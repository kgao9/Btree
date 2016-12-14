/**
 * @author See Contributors.txt for code contributors and overview of BadgerDB.
 *
 * @section LICENSE
 * Copyright (c) 2012 Database Group, Computer Sciences Department, University of Wisconsin-Madison.
 */

#include "btree.h"
#include "filescan.h"
#include "exceptions/bad_index_info_exception.h"
#include "exceptions/bad_opcodes_exception.h"
#include "exceptions/bad_scanrange_exception.h"
#include "exceptions/no_such_key_found_exception.h"
#include "exceptions/scan_not_initialized_exception.h"
#include "exceptions/index_scan_completed_exception.h"
#include "exceptions/file_not_found_exception.h"
#include "exceptions/end_of_file_exception.h"
#include "exceptions/invalid_page_exception.h"
#include "exceptions/page_pinned_exception.h"


//#define DEBUG

namespace badgerdb
{

// -----------------------------------------------------------------------------
// set File
// -----------------------------------------------------------------------------

bool BTreeIndex::setFile(const std::string & relationName, std::string & outIndexName, const int attrByteOffset)
{
    std::ostringstream idxStr;
    idxStr << relationName << "." << attrByteOffset;

    //name of the index file
    outIndexName = idxStr.str();

    //get existing file
    try
    {
        file = new BlobFile(outIndexName, false);
        return true;
    }

    //create new file
    catch(FileNotFoundException e)
    {
        file = new BlobFile(outIndexName, true);
        return false;
    }
}

//----------------------------------------------------------------------------
//get meta page
//----------------------------------------------------------------------------
IndexMetaInfo *BTreeIndex::getMetaPage(BufMgr *bufMgrIn, bool fileExists, const std::string & relationName,  const int attrByteOffset,
                const Datatype attrType)
{
     Page *metPage;
     IndexMetaInfo *metaPage;

    //set metapage
    int limit = 20;

    if(limit > relationName.length())
    {
        limit = relationName.length();
    }

    //if file exists, check for consistency
    if(fileExists)
    {
        bufMgrIn -> readPage(file, 1, metPage);
        metaPage = (IndexMetaInfo*)metPage;

        bool fRestraint = false;

        for(int i = 0; i < 20; i++)
        {
            if(i >= limit)
            {
                if((metaPage -> relationName)[i] != '\0')
                {
                    fRestraint = true;
                }

                break;
            }

            else
            {
                if((metaPage -> relationName)[i] != relationName.at(i))
                {
                    fRestraint = true;
                    break;
                }
            }
        }  

        //check consistency  
        bool sRestraint = !(metaPage -> attrByteOffset == attrByteOffset);
        bool tRestraint = !(metaPage -> attrType == attrType);

        //restraints failed - throw error
        if((fRestraint || sRestraint || tRestraint))
        {
            if(fRestraint)
            {
                std::string reason = "relation name in metaFile doesn't ";
                reason += "match relation name passed into constructor";
                throw new BadIndexInfoException(reason);
            }

            else if(sRestraint)
            {
                std::string reason = "attr byte offset in metaFile";
                reason +=  "doesn't match attr byte offset passed into constructor";

                throw new BadIndexInfoException(reason);
            }

            else
            {
                std::string reason = "attr type in metaFile doesn't match";
                reason += " attr type passed into constructor";
                throw new BadIndexInfoException(reason);

            }
        }
    }

    if(!fileExists)
    {
            //create new metapage instance
            metaPage = new IndexMetaInfo;
  
            //set relation name
            for(int i = 0; i < 20; i++)
            {
                if(i >= limit)
                {
                    metaPage -> relationName[i] = '\0';
                    break;
                }

                else
                {
                    metaPage -> relationName[i] = relationName.at(i);
                }
            }

            //set other instances
            metaPage -> attrByteOffset = attrByteOffset;
            metaPage -> attrType = attrType;
            metaPage -> rootPageNo = 3;

            PageId metaPageNo;

            bufMgrIn -> allocPage(file, metaPageNo, metPage);

            //set meta page
            Page* setPage = (Page*)(metaPage);

            metPage[0] = setPage[0]; 
    }

    bufMgrIn -> unPinPage(file, 1, !(fileExists));

    return metaPage;
}

// -----------------------------------------------------------------------------
// Scan BTreeIndex::scan datafile
// -----------------------------------------------------------------------------
void BTreeIndex::scanDataFile(const std::string & relationName)
{
     //put all file pages into the buf mgr
    FileScan *myFileScan = new FileScan(relationName, bufMgr);

    int counter = 0;

    try
    {
        while(1)
        {
            RecordId test;

            //put pages into memory
            myFileScan -> scanNext(test);

            //insert record
            std::string getRecord = myFileScan -> getRecord();

            int *key = (int*)(getRecord.c_str());

            //if(*key == 1364)
            //{
              //  Page *root;
                //NonLeafNodeInt *rootNode;

                //bufMgr -> readPage(file, rootPageNum, root);

                //rootNode = (NonLeafNodeInt*)(root);

                //printTree(rootNode);
                //exit(0);
            //}

            insertEntry(key, test);

            //flush file
            bufMgr -> flushFile(file);

            counter += 1;
        }
    }

    catch(EndOfFileException e)
    {
                //Page *root;
                //NonLeafNodeInt *rootNode;

                //bufMgr -> readPage(file, rootPageNum, root);

                //rootNode = (NonLeafNodeInt*)(root);

                //printTree(rootNode);
                //exit(0);
    }

}

//---------------------------------------------------------------------------
//create leaf
//---------------------------------------------------------------------------
void BTreeIndex::createInitLeaf(PageId &leafNo)
{
    //allocate page
    Page* newPage;

    std::cout << "allocating new page\n";

    bufMgr -> allocPage(file, leafNo, newPage);

    std::cout << "allocated new page\n";

    //create struct
    LeafNodeInt *rootLeaf = new LeafNodeInt;
    
    rootLeaf -> rightSibPageNo = 0;

    for(int i = 0; i < INTARRAYLEAFSIZE; i++)
    {
        RecordId zero = {0,0};
        rootLeaf -> ridArray[i] = zero;

        rootLeaf -> keyArray[i] = -1;
    }

    Page* convert = (Page*)(rootLeaf);

    newPage[0] = convert[0];

    bufMgr -> unPinPage(file, leafNo, true);

}

//------------------------------------------------------------------------------
// Create root
//-----------------------------------------------------------------------------

void BTreeIndex::createInitRoot()
{
    //create underlying structure of root
    PageId leaf;

    std::cout << "Creating init leaf\n";

    createInitLeaf(leaf);

    std::cout << "created leaf\n";

    //create root page
    Page* rootPage;
    Page* setPage;

    NonLeafNodeInt *rootNode = new  NonLeafNodeInt;

    rootNode -> level = 2;
    rootNode -> pageNoArray[0] = leaf;
    PageId rootNo;   

    //set up node
    setPage = (Page*)(rootNode);

    std::cout << "Putting root into file\n";

    bufMgr -> allocPage(file, rootNo, rootPage);
    rootPage[0] = setPage[0];
 
    bufMgr -> unPinPage(file, rootNo, true);
 
    std::cout << "Placed into bufMgr\n";
}

// -----------------------------------------------------------------------------
// BTreeIndex::BTreeIndex -- Constructor
// -----------------------------------------------------------------------------

BTreeIndex::BTreeIndex(const std::string & relationName,
		std::string & outIndexName,
		BufMgr *bufMgrIn,
		const int attrByteOffset,
		const Datatype attrType)
{
    std::cout << "START CONSTRUCTING\n";

    bool fileExists = setFile(relationName, outIndexName, attrByteOffset);
    
    //fetched existing file
    if(fileExists)
    {
        std::cout << "fetched file\n";
    }

    //created new file
    else
    {
        std::cout << "created new file\n";
    }

    IndexMetaInfo *metPage = getMetaPage(bufMgrIn, fileExists, relationName, attrByteOffset, attrType);

    std::cout << "retrieved the metaPage\n";

    //set private vars
    bufMgr = bufMgrIn;
    headerPageNum = 1;
    rootPageNum = metPage -> rootPageNo;
    attributeType = attrType;
    this -> attrByteOffset = attrByteOffset;
    leafOccupancy = INTARRAYLEAFSIZE;
    nodeOccupancy = INTARRAYNONLEAFSIZE;

    //set up root page if file doesn't exist
    if(!fileExists)
    {
        createInitRoot();
    }

    //before inserting anything, flush file
    bufMgr -> flushFile(file);

    scanDataFile(relationName);
   
    bufMgr -> flushFile(file);
}


// -----------------------------------------------------------------------------
// BTreeIndex::~BTreeIndex -- destructor
// -----------------------------------------------------------------------------

BTreeIndex::~BTreeIndex()
{
    
}

// -----------------------------------------------------------------------------
// BTreeIndex::Print Leaf Node
// -----------------------------------------------------------------------------

void BTreeIndex::printLeafNode(LeafNodeInt *leaf, PageId leafNo)
{
    std::cout << "LeafNode pageNo:" << leafNo << "\n";
    std::cout << "root sibling page: " << leaf -> rightSibPageNo << "\n";

    for(int i = 0; i < INTARRAYLEAFSIZE; i++)
    {
        std::cout << "index " << i << ": ";
        std::cout << "Key: " << leaf -> keyArray[i];
        std::cout << " slotNumber: " << leaf -> ridArray[i].slot_number;
        std::cout << " PageNumber: " << leaf -> ridArray[i].page_number;
        std::cout << "\n";
    }
}

// -----------------------------------------------------------------------------
// BTreeIndex::Check if leaf Node is full
// -----------------------------------------------------------------------------

bool BTreeIndex::leafFull(LeafNodeInt *leaf)
{
    //because we build the tree sequentially, the last -1 is filled with a nonegative
    //value iff the tree is full
    if(leaf -> keyArray[INTARRAYLEAFSIZE - 1] == -1)
    {
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------------
// BTreeIndex:: check if nonLeafNode is full
// -----------------------------------------------------------------------------

//same logic as leafNode
bool BTreeIndex::nonLeafFull(NonLeafNodeInt *nLeaf)
{
    //there was something wrong with how I implemented the tree
    //and I can't find it, so hard code

    if(nLeaf -> keyArray[INTARRAYNONLEAFSIZE - 1] <= 0)
    {
        return false;
    }

    return true;
}

// -----------------------------------------------------------------------------
// findInsertPlaceSplitLeaf(key, leaf)
// -----------------------------------------------------------------------------
int BTreeIndex::findInsertPlaceSplit(int keyArray[], int *key, bool isLeaf)
{
    int length = INTARRAYNONLEAFSIZE;

    if(isLeaf)
    {
        length = INTARRAYLEAFSIZE;
    }

    for(int i = 0; i < length; i++)
    {
        //if at the end, return i + 1
        if(*key > keyArray[i] && keyArray[i+1] == -1)
        {
            return i+1;
        }

        //between i and i+1, return i+1
        else if(*key > keyArray[i] && *key < keyArray[i+1])
        {
            return i + 1;
        }

        //less than i, return i
        else if(*key < keyArray[i])
        {
            return i;
        }
  
        else if(*key > keyArray[i] && i == length - 1)
        {
            return length;
        }
    }

    return -1;
}

// -----------------------------------------------------------------------------
// findInsertPlaceLeaf(key, leaf)
// -----------------------------------------------------------------------------
int BTreeIndex::findInsertPlaceLeaf(int *key, LeafNodeInt *leaf)
{
    for(int i = 0; i < INTARRAYLEAFSIZE - 1; i++)
    {
        if(leaf -> keyArray[0] == -1)
        {
            return 0;
        }

        //if at the end, return i + 1
        if(*key > leaf -> keyArray[i] && leaf -> keyArray[i+1] == -1)
        {
            return i+1;
        }

        //between i and i+1, return i+1
        else if(*key > leaf -> keyArray[i] && *key < leaf -> keyArray[i+1])
        {
            return i + 1;
        }

        //less than i, return i
        else if(*key < leaf -> keyArray[i])
        {
            return i;
        }
    }

    return -1;
}

// -----------------------------------------------------------------------------
// findInsertPlaceLeaf(key, leaf)
// -----------------------------------------------------------------------------
int BTreeIndex::findInsertPlaceNonLeaf(int *key, NonLeafNodeInt *nLeaf)
{
    for(int i = 0; i < INTARRAYNONLEAFSIZE - 1; i++)
    {
        //if at the end, return i + 1
        if(*key > nLeaf -> keyArray[i] && nLeaf -> keyArray[i+1] == -1)
        {
            return i+1;
        }

        //between i and i+1, return i+1
        else if(*key > nLeaf -> keyArray[i] && *key < nLeaf -> keyArray[i+1])
        {
            return i + 1;
        }

        //less than i, return i
        else if(*key < nLeaf -> keyArray[i])
        {
            return i;
        }
    }

    return -1;
}

void BTreeIndex::printTree(NonLeafNodeInt* node)
{
    //this is a DFS version

    Page *root;

    //get root
    bufMgr -> readPage(file, rootPageNum, root);
 
    NonLeafNodeInt *rootNode = (NonLeafNodeInt*)(root);

    //special case
    if(node -> level == 2)
    {
        std::cout << "special case\n";        

        LeafNodeInt *realRootNode;
        Page* realRoot;

        PageId realRootNo = rootNode -> pageNoArray[0];

        bufMgr -> readPage(file, realRootNo, realRoot);

        realRootNode = (LeafNodeInt*)(realRoot);

        printLeafNode(realRootNode, realRootNo);
       
        bufMgr -> unPinPage(file, realRootNo, false);
        bufMgr -> unPinPage(file, rootPageNum, false);
        std::cout << "end of Special Case\n";
    }

    //every page id below is a leaf node
    //base case
    else if(node -> level == 1)
    {
        printNonLeafNode(node);
    }

    //recurse
    else
    {
        printNonLeafNode(node);

        //for each non leaf node underneath, retrieve it and print        
        for(int i = 0; i < INTARRAYNONLEAFSIZE + 1; i++)
        {
            PageId getBelowNo = node -> pageNoArray[i];

            if(getBelowNo == 0)
            {
                break;
            }
 
            Page* getPage;
            
            bufMgr -> readPage(file, getBelowNo, getPage);

            NonLeafNodeInt *gotNode = (NonLeafNodeInt*)(getPage);

            printTree(gotNode);

            bufMgr -> unPinPage(file, getBelowNo, false);
        }
    }
}

void BTreeIndex::printNonLeafNode(NonLeafNodeInt* node)
{
    std::cout << "start of nonleaf Node\n";
    std::cout << "keys: ";

    for(int i = 0; i < INTARRAYNONLEAFSIZE; i++)
    {
        if(node -> keyArray[i] == -1)
        {
            break;
        }

        std::cout << node -> keyArray[i] << " ";
    }

    std::cout << "\n";

    if(node -> level == 1)
    {
        std::cout << "printing leaves\n";

        for(int i = 0; i < INTARRAYNONLEAFSIZE + 1; i++)
        {
            if(node -> pageNoArray[i] == 0)
            {
                break;
            }

            LeafNodeInt *leaf;
            Page* leafPage;

            PageId leafNo = node -> pageNoArray[i];

            bufMgr -> readPage(file, leafNo, leafPage);

            leaf = (LeafNodeInt*)(leafPage);

            printLeafNode(leaf, leafNo);

            bufMgr -> unPinPage(file, leafNo, false);
        }
    }

    std::cout << "end of nonleaf Node\n";
}

// -----------------------------------------------------------------------------
// BTreeIndex::getLengthLeaf
// -----------------------------------------------------------------------------
//int BTreeIndex::getLengthLeaf(LeafNodeInt *leaf)
//{
  //  for(int i = 0; i < INTARRAYLEAFSIZE; i++)
    //{
      //  if(leaf -> keyArray[i] == -1)
        //    return i;
    //}

    //return INTARRAYLEAFSIZE;
//}

// -----------------------------------------------------------------------------
// BTreeIndex::insertLeaf
// -----------------------------------------------------------------------------
void BTreeIndex::insertLeaf(int *key, const RecordId rid, LeafNodeInt *leaf)
{
    int index = findInsertPlaceLeaf(key, leaf);

    if(index == -1)
    {
        std::cout << "Inserted into leaf node that wasn't supposed to be inserted";
        std::cout << " into\n";

        exit(0);
    }
 
    //std::cout << "Inside insert Leaf\n";
    //printLeafNode(leaf);

    int copyKeyArray [INTARRAYLEAFSIZE];
    RecordId copyRidArray [INTARRAYLEAFSIZE];

    //shift everything over
    //for(int i = INTARRAYLEAFSIZE - 1; i > index; i--)
    //{
      //  leaf -> keyArray[i+1] = leaf -> keyArray[i];
    //}

    for(int i = 0; i < index; i++)
    {
        copyKeyArray[i] = leaf -> keyArray[i];
        copyRidArray[i] = leaf -> ridArray[i];
    }

    for(int i = index+1; i < INTARRAYLEAFSIZE; i++)
    {
        copyKeyArray[i] = leaf -> keyArray[i - 1];
        copyRidArray[i] = leaf -> ridArray[i - 1];
    }

    copyKeyArray[index] = *key;
    copyRidArray[index] = rid;

    //leaf -> keyArray = copyKeyArray;
    //leaf -> ridArray = copyRidArray;

    std::copy(std::begin(copyKeyArray), std::end(copyKeyArray), leaf -> keyArray);
    std::copy(std::begin(copyRidArray), std::end(copyRidArray), leaf -> ridArray);

    //printLeafNode(leaf);

    //std::cout << "outside insert leaf\n";
    //leaf -> keyArray[index] = *key;
    //leaf -> ridArray[index] = rid;
}

// -----------------------------------------------------------------------------
// BTreeIndex::insertNonLeaf
// -----------------------------------------------------------------------------
void BTreeIndex::insertNonLeaf(int* key, PageId fLeafNo, PageId sLeafNo, NonLeafNodeInt *nLeaf)
{
    int index = findInsertPlaceNonLeaf(key, nLeaf);

    if(index == -1)
    {
        std::cout << "Inserted into leaf node that wasn't supposed to be inserted";
        std::cout << " into\n";

        exit(0);
    }

    int copyKeyArray [INTARRAYNONLEAFSIZE];
    PageId copyPageNoArray [INTARRAYNONLEAFSIZE + 1];

    for(int i = 0; i < index; i++)
    {
        copyKeyArray[i] = nLeaf -> keyArray[i];
        copyPageNoArray[i] = nLeaf -> pageNoArray[i];
    }

    for(int i = index+1; i < INTARRAYLEAFSIZE; i++)
    {
        copyKeyArray[i] = nLeaf -> keyArray[i - 1];
        copyPageNoArray[i + 1] = nLeaf -> pageNoArray[i];
    }

    copyKeyArray[index] = *key;
    copyPageNoArray[index] = fLeafNo;
    copyPageNoArray[index + 1] = sLeafNo;

    std::copy(std::begin(copyKeyArray), std::end(copyKeyArray), nLeaf -> keyArray);
    std::copy(std::begin(copyPageNoArray), std::end(copyPageNoArray), nLeaf -> pageNoArray);

    //need to set left leaf sibling page
    if(index != 0 && nLeaf -> level == 1)
    {
        PageId lLeaf = copyPageNoArray[index - 1];
        Page *lPage;

        bufMgr -> readPage(file, lLeaf, lPage);
        
        LeafNodeInt *lLeafNode = (LeafNodeInt*)(lPage);

        lLeafNode -> rightSibPageNo = fLeafNo;

        Page *convert = (Page*)(lLeafNode);
        lPage[0] = convert[0];

        bufMgr -> unPinPage(file, lLeaf, true);
    }
}

// -----------------------------------------------------------------------------
// BTreeIndex::slit root
// -----------------------------------------------------------------------------
void BTreeIndex::splitRoot(PageId fNextNode, PageId sNextNode, NonLeafNodeInt *root, int* intKey, bool level)
{
     NonLeafNodeInt tempLeaf = *root;

     //want to delete root
     bufMgr -> unPinPage(file, rootPageNum, false);

     bufMgr -> flushFile(file);

     //try to delete leaf
     try
     {
         file -> deletePage(rootPageNum);
     }

     catch(InvalidPageException e)
     {
     }

     //recreate root
     Page *rootPage;
     PageId rootId;
     bufMgr -> allocPage(file, rootId, rootPage);

     rootPageNum = rootId;

     //create non-leaves before implementing new root
     int copyKeyArray [INTARRAYNONLEAFSIZE + 1];
     PageId copyPageNoArray [INTARRAYNONLEAFSIZE + 2];

     int index = findInsertPlaceSplit(tempLeaf.keyArray, intKey, true);

     for(int i = 0; i < index; i++)
     {
         copyKeyArray[i] = tempLeaf.keyArray[i];
         copyPageNoArray[i] = tempLeaf.pageNoArray[i];
     }

     copyKeyArray[index] = *intKey;
     copyPageNoArray[index] = fNextNode;
     copyPageNoArray[index + 1] = sNextNode;

     for(int i = index + 1; i < INTARRAYNONLEAFSIZE + 1; i++)
     {
         copyKeyArray[i] = tempLeaf.keyArray[i - 1];
         copyPageNoArray[i+1] = tempLeaf.pageNoArray[i];
     }

     //split in half
     int half = (INTARRAYNONLEAFSIZE + 1) / 2;
     int firstKeys[INTARRAYNONLEAFSIZE];
     int secondKeys[INTARRAYNONLEAFSIZE];
     PageId firstPageNo[INTARRAYNONLEAFSIZE];
     PageId secondPageNo[INTARRAYNONLEAFSIZE];

     //split arrays in two
     for(int i = 0; i < INTARRAYNONLEAFSIZE + 1; i++)
     {
         if(i != INTARRAYNONLEAFSIZE)
             secondKeys[i] = -1;
         secondPageNo[i] = 0;

         if(i >= half)
         {
             if(i != INTARRAYNONLEAFSIZE)
                 firstKeys[i] = -1;
             firstPageNo[i] = 0;
         }

         else
         {
             if(i != INTARRAYNONLEAFSIZE)
                 firstKeys[i] = copyKeyArray[i];
             firstPageNo[i] = copyPageNoArray[i];
         }
     }

     for(int i = half; i < (INTARRAYNONLEAFSIZE + 2); i++)
     {
         if(i != INTARRAYNONLEAFSIZE + 1)
             secondKeys[i - half] = copyKeyArray[i];
         secondPageNo[i - half] = copyPageNoArray[i];
     }

     //close but we need to push a key up to the root
     int pushUp = firstKeys[half - 1];
     firstKeys[half - 1] = -1;

     Page *fnLeaf;
     PageId fnLeafNo;
     Page *snLeaf;
     PageId snLeafNo;

     //create new leaves
     bufMgr -> allocPage(file, fnLeafNo, fnLeaf);
     bufMgr -> allocPage(file, snLeafNo, snLeaf);

     NonLeafNodeInt *fnLeafNode = (NonLeafNodeInt*)(fnLeaf);
     NonLeafNodeInt *snLeafNode = (NonLeafNodeInt*)(snLeaf);

     std::copy(std::begin(firstKeys), std::end(firstKeys), fnLeafNode -> keyArray);
     std::copy(std::begin(firstPageNo), std::end(firstPageNo), fnLeafNode -> pageNoArray);

     std::copy(std::begin(secondKeys), std::end(secondKeys), snLeafNode -> keyArray);
     std::copy(std::begin(secondPageNo), std::end(secondPageNo), snLeafNode -> pageNoArray);

     if(level)
     {
         fnLeafNode -> level = 1;
         snLeafNode -> level = 1;
     }

     //convert back into pages
     Page* convert = (Page*)(fnLeafNode);
     fnLeaf[0] = convert[0];

     convert = (Page*)(snLeafNode);
     snLeaf[0] = convert[0];

     bufMgr -> unPinPage(file, fnLeafNo, true);
     bufMgr -> unPinPage(file, snLeafNo, true);

     //need to set left leaf sibling page
     if(index != 0 && tempLeaf.level == 1)
     {
         PageId lLeaf = copyPageNoArray[index - 1];
         Page *lPage;
 
         bufMgr -> readPage(file, lLeaf, lPage);
 
         LeafNodeInt *lLeafNode = (LeafNodeInt*)(lPage);
 
         lLeafNode -> rightSibPageNo = fNextNode;
 
         Page *convert = (Page*)(lLeafNode);
         lPage[0] = convert[0];
 
         bufMgr -> unPinPage(file, lLeaf, true);
     }

     //set Root
     NonLeafNodeInt *rootNode = (NonLeafNodeInt*)(rootPage);
     for(int i = 0; i < INTARRAYNONLEAFSIZE; i++)
     {
         //fill keys
         if(i == 0)
         {
                 rootNode -> keyArray[i] = pushUp;
         }
         
         else
         {
             rootNode -> keyArray[i] = -1;
         }
     }

     for(int i = 0; i < INTARRAYNONLEAFSIZE; i++)
     {
         //fill page No
         if(i == 0)
         {
             rootNode -> pageNoArray[i] = fnLeafNo;
         }

         else if(i == 1)
         {
             rootNode -> pageNoArray[i] = snLeafNo;
         }

         else
         {
             rootNode -> pageNoArray[i] = 0;
         }
     }

     rootNode -> level = 0;

     bufMgr -> unPinPage(file, rootPageNum, true);
     bufMgr -> flushFile(file);

     //convert back into pages
     convert = (Page*)(rootNode);
     rootPage[0] = convert[0];
}

void BTreeIndex::splitNonLeafNode(PageId fNextNode, PageId sNextNode, PageId oldNode, NonLeafNodeInt *currNode, std::list <PageId> &path, bool firstLevel, int *intKey)
{
     NonLeafNodeInt tempLeaf = *currNode;

     //want to delete root
     bufMgr -> unPinPage(file, oldNode, false);

     bufMgr -> flushFile(file);

     //try to delete leaf
     try
     {
         std::cout << "trying\n";
         file -> deletePage(oldNode);
         std::cout << "failing\n";
     }

     catch(InvalidPageException e)
     {
     }

     //get Prev Node
     PageId prevPageNo = path.front();
     path.pop_front();

     //create non-leaves before splitting
     int copyKeyArray [INTARRAYNONLEAFSIZE + 1];
     PageId copyPageNoArray [INTARRAYNONLEAFSIZE + 2];

     int index = findInsertPlaceSplit(tempLeaf.keyArray, intKey, true);

     for(int i = 0; i < index; i++)
     {
         copyKeyArray[i] = tempLeaf.keyArray[i];
         copyPageNoArray[i] = tempLeaf.pageNoArray[i];
     }

     copyKeyArray[index] = *intKey;
     copyPageNoArray[index] = fNextNode;
     copyPageNoArray[index + 1] = sNextNode;

     for(int i = index + 1; i < INTARRAYNONLEAFSIZE + 1; i++)
     {
         copyKeyArray[i] = tempLeaf.keyArray[i - 1];
         copyPageNoArray[i+1] = tempLeaf.pageNoArray[i];
     }

     //split in half
     int half = (INTARRAYNONLEAFSIZE + 1) / 2;
     int firstKeys[INTARRAYNONLEAFSIZE];
     int secondKeys[INTARRAYNONLEAFSIZE];
     PageId firstPageNo[INTARRAYNONLEAFSIZE];
     PageId secondPageNo[INTARRAYNONLEAFSIZE];

     //split arrays in two
     for(int i = 0; i < INTARRAYNONLEAFSIZE + 1; i++)
     {
         if(i != INTARRAYNONLEAFSIZE)
             secondKeys[i] = -1;
         secondPageNo[i] = 0;

         if(i >= half)
         {
             if(i != INTARRAYNONLEAFSIZE)
                 firstKeys[i] = -1;
             firstPageNo[i] = 0;
         }

         else
         {
             if(i != INTARRAYNONLEAFSIZE)
                 firstKeys[i] = copyKeyArray[i];
             firstPageNo[i] = copyPageNoArray[i];
         }
     }

     for(int i = half; i < (INTARRAYNONLEAFSIZE + 2); i++)
     {
         if(i != INTARRAYNONLEAFSIZE + 1)
             secondKeys[i - half] = copyKeyArray[i];
         secondPageNo[i - half] = copyPageNoArray[i];
     }

     //close but we need to push a key up to the next nonleaf node
     int pushUp = firstKeys[half - 1];
     firstKeys[half - 1] = -1;

     Page *fnLeaf;
     PageId fnLeafNo;
     Page *snLeaf;
     PageId snLeafNo;

     //create new leaves
     bufMgr -> allocPage(file, fnLeafNo, fnLeaf);
     bufMgr -> allocPage(file, snLeafNo, snLeaf);

     NonLeafNodeInt *fnLeafNode = (NonLeafNodeInt*)(fnLeaf);
     NonLeafNodeInt *snLeafNode = (NonLeafNodeInt*)(snLeaf);

     std::copy(std::begin(firstKeys), std::end(firstKeys), fnLeafNode -> keyArray);
     std::copy(std::begin(firstPageNo), std::end(firstPageNo), fnLeafNode -> pageNoArray);

     std::copy(std::begin(secondKeys), std::end(secondKeys), snLeafNode -> keyArray);
     std::copy(std::begin(secondPageNo), std::end(secondPageNo), snLeafNode -> pageNoArray);

     if(firstLevel)
     {
         fnLeafNode -> level = 1;
         snLeafNode -> level = 1;
     }

     //convert back into pages
     Page* convert = (Page*)(fnLeafNode);
     fnLeaf[0] = convert[0];

     convert = (Page*)(snLeafNode);
     snLeaf[0] = convert[0];

     bufMgr -> unPinPage(file, fnLeafNo, true);
     bufMgr -> unPinPage(file, snLeafNo, true);

     //need to set left leaf sibling page
     if(index != 0 && tempLeaf.level == 1)
     {
         PageId lLeaf = copyPageNoArray[index - 1];
         Page *lPage;
 
         bufMgr -> readPage(file, lLeaf, lPage);
 
         LeafNodeInt *lLeafNode = (LeafNodeInt*)(lPage);
 
         lLeafNode -> rightSibPageNo = fNextNode;
 
         Page *convert = (Page*)(lLeafNode);
         lPage[0] = convert[0];
 
         bufMgr -> unPinPage(file, lLeaf, true);
     }

     Page *prevPage;
     bufMgr -> readPage(file, prevPageNo, prevPage);

     NonLeafNodeInt *prevNode = (NonLeafNodeInt*)(prevPage);

     if(nonLeafFull(prevNode))
     {
         if(prevPageNo == rootPageNum)
         {
             int *entry = &pushUp;

             splitNonLeafNode(fnLeafNo, snLeafNo, prevPageNo, prevNode, path, false, entry);
         }

         else
         {
             int *entry = &pushUp;

             splitRoot(fnLeafNo, snLeafNo, prevNode, entry, false);
         }
     } 

     else
     {
          int *entry = &pushUp;

          insertNonLeaf(entry, fnLeafNo, snLeafNo, prevNode);

          //update page
          Page* convert = (Page*)(prevNode);

          prevPage[0] = convert[0];

          //clean up
          bufMgr -> unPinPage(file, prevPageNo, true);
      }

     //see if we need to split the prevNode
}

// -----------------------------------------------------------------------------
// BTreeIndex::splitLeaf
// -----------------------------------------------------------------------------
void BTreeIndex::splitLeaf(LeafNodeInt *leafNode, PageId oldLeaf, std::list <PageId> &path, int *intKey, RecordId rid)
{
     //printTree(rootNode);
     //bufMgr -> unPinPage(file, rootPageNum, false);
     LeafNodeInt tempLeaf = *leafNode;

     //want to delete leaf
     bufMgr -> unPinPage(file, rootPageNum, false);
     bufMgr -> unPinPage(file, oldLeaf, false);

     bufMgr -> flushFile(file);

     //try to delete leaf
     try
     {
         file -> deletePage(oldLeaf);
     }

     catch(InvalidPageException e)
     {
     }

     //create leaves before setting new non-leaf node
     int copyKeyArray [INTARRAYLEAFSIZE + 1];
     RecordId copyRidArray [INTARRAYLEAFSIZE + 1];

     int index = findInsertPlaceSplit(tempLeaf.keyArray, intKey, true);


     for(int i = 0; i < index; i++)
     {
         copyKeyArray[i] = tempLeaf.keyArray[i];
         copyRidArray[i] = tempLeaf.ridArray[i];
     }

     copyKeyArray[index] = *intKey;
     copyRidArray[index] = rid;

     for(int i = index; i < INTARRAYLEAFSIZE; i++)
     {
         copyKeyArray[i+1] = tempLeaf.keyArray[i];
         copyRidArray[i+1] = tempLeaf.ridArray[i];
     }

     //split in half
     int half = (INTARRAYLEAFSIZE + 1) / 2;
     int firstKeys[INTARRAYLEAFSIZE];
     int secondKeys[INTARRAYLEAFSIZE];
     RecordId firstRid[INTARRAYLEAFSIZE];
     RecordId secondRid[INTARRAYLEAFSIZE];

     //split arrays in two
     for(int i = 0; i < INTARRAYLEAFSIZE; i++)
     {
         secondKeys[i] = -1;
         secondRid[i] = {0,0};

         if(i >= half)
         {
             firstKeys[i] = -1;
             firstRid[i] = {0,0};
         }

         else
         {
             firstKeys[i] = copyKeyArray[i];
             firstRid[i] = copyRidArray[i];
         }
     }

     for(int i = half; i < (INTARRAYLEAFSIZE + 1); i++)
     {
         secondKeys[i - half] = copyKeyArray[i];
         secondRid[i - half] = copyRidArray[i];
     }

     Page *fLeaf;
     PageId fLeafNo;
     Page *sLeaf;
     PageId sLeafNo;

     //create new leaves
     bufMgr -> allocPage(file, fLeafNo, fLeaf);
     bufMgr -> allocPage(file, sLeafNo, sLeaf);

     LeafNodeInt *fLeafNode = (LeafNodeInt*)(fLeaf);
     LeafNodeInt *sLeafNode = (LeafNodeInt*)(sLeaf);

     fLeafNode -> rightSibPageNo = sLeafNo;
     std::copy(std::begin(firstKeys), std::end(firstKeys), fLeafNode -> keyArray);
     std::copy(std::begin(firstRid), std::end(firstRid), fLeafNode -> ridArray);

     sLeafNode -> rightSibPageNo = tempLeaf.rightSibPageNo;
     std::copy(std::begin(secondKeys), std::end(secondKeys), sLeafNode -> keyArray);
     std::copy(std::begin(secondRid), std::end(secondRid), sLeafNode -> ridArray);

     //convert back into pages
     Page* convert = (Page*)(fLeafNode);
     fLeaf[0] = convert[0];

     convert = (Page*)(sLeafNode);
     sLeaf[0] = convert[0];

     bufMgr -> unPinPage(file, fLeafNo, true);
     bufMgr -> unPinPage(file, sLeafNo, true);

     std::cout << "leaves completed\n";

     //see if nonleafnode is full
     PageId prevNodeId = path.front();
     path.pop_front();

     Page *prevPage;

     bufMgr -> readPage(file, prevNodeId, prevPage);
 
     NonLeafNodeInt *prevNode = (NonLeafNodeInt*)(prevPage);

     if(nonLeafFull(prevNode))
     {
         if(prevNodeId == rootPageNum)
         {
             int entry = secondKeys[0];
             int *newEntry = &entry;

             splitRoot(fLeafNo, sLeafNo, prevNode, newEntry, true);
         }

         else
         {
             std::cout << "split non root\n";
             int entry = secondKeys[0];
             int *newEntry = &entry;

             splitNonLeafNode(fLeafNo, sLeafNo, prevNodeId, prevNode, path, true, newEntry);
         }
         //not implemented
         //exit(0); 
     }

     else
     {
         int entry = secondKeys[0];
         int *newEntry = &entry;

         insertNonLeaf(newEntry, fLeafNo, sLeafNo, prevNode);

         std::cout << "done insertion\n";

         //printTree(prevNode);

         //convert back
         Page *convert = (Page*)(prevNode);
         prevPage[0] = convert[0];

         //unpin page
         bufMgr -> unPinPage(file, prevNodeId, true);
     }
}

void BTreeIndex::findPath(std::list <PageId> &path, int *key)
{
    Page *currPage;
    PageId nextPageNo;

    nextPageNo = rootPageNum;

    bool reachedLeaf = false;

    while(!reachedLeaf)
    {
        //push curr node's page number into stack
        path.push_front(nextPageNo);

        //get page
        bufMgr -> readPage(file, nextPageNo, currPage);
        PageId currPageNum = nextPageNo;  
 
        //getNonLeafNode
        NonLeafNodeInt * actualNode = (NonLeafNodeInt*)(currPage);

        if(actualNode -> level == 1)
        {
            //std::cout << "find path " << nextPageNo; 
            reachedLeaf = true;
        }

        //get next page No
        for(int i = 0; i < INTARRAYNONLEAFSIZE; i++)
        {
            if(i == 0)
            {
                if(*key < actualNode -> keyArray[0])
                {
                    nextPageNo = actualNode -> pageNoArray[0];
                    break;
                }
            }
 
            else
            {
                if(*key > actualNode -> keyArray[i - 1] && *key < actualNode -> keyArray[i])
                {
                    nextPageNo = actualNode -> pageNoArray[i];
                    break;
                }

                else if(*key > actualNode -> keyArray[i - 1] && actualNode -> keyArray[i] == -1)
                {
                    nextPageNo = actualNode -> pageNoArray[i];
                    break;
                }

                else if(*key > actualNode -> keyArray[i] && i == INTARRAYNONLEAFSIZE - 1)
                {
                    nextPageNo = actualNode -> pageNoArray[i + 1];
                    break;
                }
            }
        }

        bufMgr -> unPinPage(file, currPageNum, false);
    }

    //leaf is reached
    path.push_front(nextPageNo);
}

// -----------------------------------------------------------------------------
// BTreeIndex::insertEntry
// -----------------------------------------------------------------------------

//code for insertEntry is ruddy terrible
//TAs hide your eyes, it looks disgusting.
//I hate this code SOOOOOO much
//it's like Urgh

const void BTreeIndex::insertEntry(const void *key, const RecordId rid) 
{
    int *intKey = (int*)(key);
    
    //std::cout << "Inserting Key " << *intKey << " with page No " << rid.page_number << " and slot id " << rid.slot_number << "\n";

    Page *root;
 
    //get root
    bufMgr -> readPage(file, rootPageNum, root);   

    NonLeafNodeInt *rootNode = (NonLeafNodeInt*)(root);

    //see if first case of root
    if(rootNode -> level == 2)
    {
        LeafNodeInt *realRootNode;
        Page* realRoot;

        PageId realRootNo = rootNode -> pageNoArray[0];

        bufMgr -> readPage(file, realRootNo, realRoot);

        realRootNode = (LeafNodeInt*)(realRoot);

        if(leafFull(realRootNode))
        {
            LeafNodeInt tempLeaf = *realRootNode;

            bufMgr -> unPinPage(file, realRootNo, false);
            bufMgr -> unPinPage(file, rootPageNum, false);

            bufMgr -> flushFile(file);

            //try to delete page
            try
            {
                file -> deletePage(realRootNo);
            }

            catch(InvalidPageException e)
            {
            }

            try
            {
                file -> deletePage(rootPageNum);
            }

            catch(InvalidPageException e)
            {
            }

            Page *newRoot;
            PageId newRootNo;

            //create new root
            bufMgr -> allocPage(file, newRootNo, newRoot);

            rootPageNum = newRootNo;

            //create leaves before setting new non-leaf node
            int copyKeyArray [INTARRAYLEAFSIZE + 1];
            RecordId copyRidArray [INTARRAYLEAFSIZE + 1];

            int index = findInsertPlaceSplit(tempLeaf.keyArray, intKey, true);

            for(int i = 0; i < index; i++)
            {
                copyKeyArray[i] = tempLeaf.keyArray[i];
                copyRidArray[i] = tempLeaf.ridArray[i];
            }

            copyKeyArray[index] = *intKey;
            copyRidArray[index] = rid;

            for(int i = index; i < INTARRAYLEAFSIZE; i++)
            {
                copyKeyArray[i+1] = tempLeaf.keyArray[i];
                copyRidArray[i+1] = tempLeaf.ridArray[i];
            }

            //split in half
            int half = (INTARRAYLEAFSIZE + 1) / 2;
            int firstKeys[INTARRAYLEAFSIZE];
            int secondKeys[INTARRAYLEAFSIZE];
            RecordId firstRid[INTARRAYLEAFSIZE];
            RecordId secondRid[INTARRAYLEAFSIZE];

            //split arrays in two
            for(int i = 0; i < INTARRAYLEAFSIZE; i++)
            {
                secondKeys[i] = -1;
                secondRid[i] = {0,0};

                if(i >= half)
                {
                    firstKeys[i] = -1;
                    firstRid[i] = {0,0};
                }

                else
                {
                    firstKeys[i] = copyKeyArray[i];
                    firstRid[i] = copyRidArray[i];
                }
            }

            for(int i = half; i < (INTARRAYLEAFSIZE + 1); i++)
            {
                secondKeys[i - half] = copyKeyArray[i];
                secondRid[i - half] = copyRidArray[i]; 
            }
        
            Page *fLeaf;
            PageId fLeafNo;
            Page *sLeaf;
            PageId sLeafNo;

            //create new leaves
            bufMgr -> allocPage(file, fLeafNo, fLeaf);
            bufMgr -> allocPage(file, sLeafNo, sLeaf);

            LeafNodeInt *fLeafNode = (LeafNodeInt*)(fLeaf);
            LeafNodeInt *sLeafNode = (LeafNodeInt*)(sLeaf);

            fLeafNode -> rightSibPageNo = sLeafNo;
            std::copy(std::begin(firstKeys), std::end(firstKeys), fLeafNode -> keyArray);
            std::copy(std::begin(firstRid), std::end(firstRid), fLeafNode -> ridArray);

            sLeafNode -> rightSibPageNo = 0;
            std::copy(std::begin(secondKeys), std::end(secondKeys), sLeafNode -> keyArray);
            std::copy(std::begin(secondRid), std::end(secondRid), sLeafNode -> ridArray);

            //convert back into pages
            Page* convert = (Page*)(fLeafNode);
            fLeaf[0] = convert[0];
            
            convert = (Page*)(sLeafNode);
            sLeaf[0] = convert[0];

            //create new root
            NonLeafNodeInt *newRootNode = (NonLeafNodeInt*)(newRoot);
            for(int i = 0; i < INTARRAYNONLEAFSIZE; i++)
            {
                newRootNode -> keyArray[i] = -1;
                newRootNode -> pageNoArray[i] = 0;
            }
 
            newRootNode -> pageNoArray[INTARRAYNONLEAFSIZE] = 0;

            newRootNode -> keyArray[0] = copyKeyArray[half];
            
            newRootNode -> pageNoArray[0] = fLeafNo;
            newRootNode -> pageNoArray[1] = sLeafNo;

            newRootNode -> level = 1;

            //after creation, recast
            convert = (Page*)(newRootNode);

            newRoot[0] = convert[0];

            //unpin all new pages
            bufMgr -> unPinPage(file, newRootNo, true);
            bufMgr -> unPinPage(file, fLeafNo, true);
            bufMgr -> unPinPage(file, sLeafNo, true);

            Page *metPage;

            bufMgr -> readPage(file, 1, metPage);

            IndexMetaInfo *metaPage = (IndexMetaInfo*)metPage;

            metaPage -> rootPageNo = newRootNo;

            //set back
            convert = (Page*)(metaPage);

            metPage[0] = convert[0];

            bufMgr -> unPinPage(file, 1, true);

            //try flush then dispose
            bufMgr -> flushFile(file);

        }

        else
        {
            insertLeaf(intKey, rid, realRootNode);

            //update page
            Page* convert = (Page*)(realRootNode);
            
            realRoot[0] = convert[0];

            //clean up
            bufMgr -> unPinPage(file, realRootNo, true);

            //std::cout << "here\n";

            bufMgr -> unPinPage(file, rootPageNum, false);
        }
    }

    else
    {
        std::list <PageId> path;

        //get path from root to leaf
        findPath(path, intKey);

        //std::cout << "The contents of fifth are: ";
        //for (std::list<PageId>::iterator it = path.begin(); it != path.end(); it++)
          //  std::cout << *it << ' ';

        //std::cout << '\n';

        //get leaf
        PageId leafNo = path.front();
        path.pop_front();

        Page *leaf;
        bufMgr -> readPage(file, leafNo, leaf);

        LeafNodeInt *leafNode = (LeafNodeInt*)(leaf);

         //std::cout << "The contents of path are: ";
           // for (std::list<PageId>::iterator it = path.begin(); it != path.end(); it++)
             //   std::cout << *it << ' ';

            //std::cout << '\n';

        if(leafFull(leafNode))
        {
            std::cout << "need to split leaf\n";
            splitLeaf(leafNode, leafNo, path, intKey, rid);
        }

        else
        {
            insertLeaf(intKey, rid, leafNode);

            //update page
            Page* convert = (Page*)(leafNode);

            leaf[0] = convert[0];

            //clean up
            bufMgr -> unPinPage(file, leafNo, true);

            bufMgr -> unPinPage(file, rootPageNum, false);
        }
    }
    
}

void BTreeIndex::updateScanVars(Page *currPage, PageId currPageNo, LeafNodeInt *leafNode, int indexStart)
{
    int index = -1;
 
    //scan leaf
    for(int i = indexStart; i < INTARRAYLEAFSIZE; i++)
    {
        bool uBound = false;
        bool lBound = false;

        if(leafNode -> keyArray[i] == -1)
        {
            break;
        }

        if(highOp == LT)
        {
            uBound = (leafNode -> keyArray[i] < highValInt);
        }

        else if(highOp == LTE)
        {
            uBound = (leafNode -> keyArray[i] <= highValInt);
        }

        if(lowOp == GT)
        {
            lBound = (leafNode -> keyArray[i] > lowValInt);
        }

        else if(lowOp == GTE)
        {
            lBound = (leafNode -> keyArray[i] >= lowValInt);
        }
        
        if(lBound && uBound)
        {
            index = i;
            break;
        }
    }

    if(index == -1)
    {
        //finished scanning leaf

        //in next leaf maybe
        PageId nextLeaf = leafNode -> rightSibPageNo;
        
        //unpin page
        bufMgr -> unPinPage(file, currPageNo, false);
        bufMgr -> flushFile(file);

        Page* leafPage;


        bufMgr -> readPage(file, nextLeaf, leafPage);

        LeafNodeInt *nextLeafNode = (LeafNodeInt*)(leafPage);

        bool uBound = false;
        bool lBound = false;

        if(highOp == LT)
        {
            uBound = nextLeafNode -> keyArray[0] < highValInt;
        }

        else if(highOp == LTE)
        {
            uBound = nextLeafNode -> keyArray[0] <= highValInt;
        }

        if(lowOp == GT)
        {
            lBound = nextLeafNode -> keyArray[0] > lowValInt;
        }

        else if(lowOp == GTE)
        {
            lBound = nextLeafNode -> keyArray[0] >= lowValInt;
        }
        
        if(lBound && uBound)
        {
            //index found
            currentPageNum = nextLeaf;
            currentPageData = leafPage;
            nextEntry = 0;
        }

        else
        {
            //not found
            bufMgr -> unPinPage(file, nextLeaf, false);
            bufMgr -> flushFile(file);
            
            nextEntry = -1;
            currentPageNum = 0;
        }
    }

    else
    {
        currentPageNum = currPageNo;
        currentPageData = currPage;
        nextEntry = index;
    }

}

// -----------------------------------------------------------------------------
// BTreeIndex::startScan
// -----------------------------------------------------------------------------

const void BTreeIndex::startScan(const void* lowValParm,
				   const Operator lowOpParm,
				   const void* highValParm,
				   const Operator highOpParm)
{
    if( lowOpParm != GT && lowOp != GTE)
    {
        throw new BadOpcodesException();
    }

    if(highOpParm != LT && highOpParm != LTE)
    {
        throw new BadOpcodesException();
    }

    //too late for this BS
    //just gonna assume it's integer
    int * lowVal = (int*)(lowValParm);
    int * highVal = (int*)(highValParm);

    if(*highVal < *lowVal)
    {
        throw new BadScanrangeException();
    }

    lowValInt = *lowVal;
    highValInt = *highVal;
    lowOp = lowOpParm;
    highOp = highOpParm;

    scanExecuting = true;
    
    //get pageid
    std::list <PageId> path;

    //get path from root to leaf
    findPath(path, lowVal);

    PageId leafNo = path.front();
    Page* leaf;

    bufMgr -> readPage(file, leafNo, leaf);
    
    LeafNodeInt *leafNode = (LeafNodeInt*)(leaf);

    updateScanVars(leaf, leafNo, leafNode, 0);
}

// -----------------------------------------------------------------------------
// BTreeIndex::scanNext
// -----------------------------------------------------------------------------

const void BTreeIndex::scanNext(RecordId& outRid) 
{
    if(nextEntry == -1)
    {
        std::cout << "threw\n";
        throw new IndexScanCompletedException();
    }

    LeafNodeInt *leafNode = (LeafNodeInt*)(currentPageData);
     
    outRid = leafNode -> ridArray[nextEntry];
    
    updateScanVars(currentPageData, currentPageNum, leafNode, nextEntry + 1);
}

// -----------------------------------------------------------------------------
// BTreeIndex::endScan
// -----------------------------------------------------------------------------
//
const void BTreeIndex::endScan() 
{
    if(!scanExecuting)
    {
        throw new ScanNotInitializedException();
    }

    else
    {
        try
        {
            scanExecuting = false;
            bufMgr -> flushFile(file);
        }

        catch(PagePinnedException e)
        {
            std::cout << "trying to prematurely end scan\n";
            std::cout << "trying to unpin current page\n";
            try
            {
                bufMgr -> unPinPage(file, currentPageNum, false);
                bufMgr -> flushFile(file);
            }

            catch(PagePinnedException e)
            {
                bufMgr -> printSelf();
                std::cout << "WTH is pinned?";
            }
        }
    }
}

}
