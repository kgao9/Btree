/**
 * @author See Contributors.txt for code contributors and overview of BadgerDB.
 *
 * @section LICENSE
 * Copyright (c) 2012 Database Group, Computer Sciences Department, University of Wisconsin-Madison.
 */

#pragma once

#include <iostream>
#include <string>
#include "string.h"
#include <sstream>

#include "types.h"
#include "page.h"
#include "file.h"
#include "buffer.h"
#include <list>

namespace badgerdb
{

/**
 * @brief Datatype enumeration type.
 */
enum Datatype
{
	INTEGER = 0,
	DOUBLE = 1,
	STRING = 2
};

/**
 * @brief Scan operations enumeration. Passed to BTreeIndex::startScan() method.
 */
enum Operator
{ 
	LT, 	/* Less Than */
	LTE,	/* Less Than or Equal to */
	GTE,	/* Greater Than or Equal to */
	GT		/* Greater Than */
};


/**
 * @brief Number of key slots in B+Tree leaf for INTEGER key.
 */
//                                                  sibling ptr             key               rid
const  int INTARRAYLEAFSIZE = ( Page::SIZE - sizeof( PageId ) ) / ( sizeof( int ) + sizeof( RecordId ) );
//const int INTARRAYLEAFSIZE = 3;

/**
 * @brief Number of key slots in B+Tree non-leaf for INTEGER key.
 */
//                                                     level     extra pageNo                  key       pageNo
const  int INTARRAYNONLEAFSIZE = ( Page::SIZE - sizeof( int ) - sizeof( PageId ) ) / ( sizeof( int ) + sizeof( PageId ) );
//const int INTARRAYNONLEAFSIZE = 3;

/**
 * @brief Structure to store a key-rid pair. It is used to pass the pair to functions that 
 * add to or make changes to the leaf node pages of the tree. Is templated for the key member.
 */
template <class T>
class RIDKeyPair{
public:
	RecordId rid;
	T key;
	void set( RecordId r, T k)
	{
		rid = r;
		key = k;
	}
};

/**
 * @brief Structure to store a key page pair which is used to pass the key and page to functions that make 
 * any modifications to the non leaf pages of the tree.
*/
template <class T>
class PageKeyPair{
public:
	PageId pageNo;
	T key;
	void set( int p, T k)
	{
		pageNo = p;
		key = k;
	}
};

/**
 * @brief Overloaded operator to compare the key values of two rid-key pairs
 * and if they are the same compares to see if the first pair has
 * a smaller rid.pageNo value.
*/
template <class T>
bool operator<( const RIDKeyPair<T>& r1, const RIDKeyPair<T>& r2 )
{
	if( r1.key != r2.key )
		return r1.key < r2.key;
	else
		return r1.rid.page_number < r2.rid.page_number;
}

/**
 * @brief The meta page, which holds metadata for Index file, is always first page of the btree index file and is cast
 * to the following structure to store or retrieve information from it.
 * Contains the relation name for which the index is created, the byte offset
 * of the key value on which the index is made, the type of the key and the page no
 * of the root page. Root page starts as page 2 but since a split can occur
 * at the root the root page may get moved up and get a new page no.
*/
struct IndexMetaInfo{
  /**
   * Name of base relation.
   */
	char relationName[20];

  /**
   * Offset of attribute, over which index is built, inside the record stored in pages.
   */
	int attrByteOffset;

  /**
   * Type of the attribute over which index is built.
   */
	Datatype attrType;

  /**
   * Page number of root page of the B+ Tree inside the file index file.
   */
	PageId rootPageNo;
};

/*
Each node is a page, so once we read the page in we just cast the pointer to the page to this struct and use it to access the parts
These structures basically are the format in which the information is stored in the pages for the index file depending on what kind of 
node they are. The level memeber of each non leaf structure seen below is set to 1 if the nodes 
at this level are just above the leaf nodes - but it is set to 2 if the node is the root and it hasn't been split yet. 
Otherwise set to 0.
*/

/**
 * @brief Structure for all non-leaf nodes when the key is of INTEGER type.
*/
struct NonLeafNodeInt{
  /**
   * Level of the node in the tree.
   */
	int level;

  /**
   * Stores keys.
   */
	int keyArray[ INTARRAYNONLEAFSIZE ];

  /**
   * Stores page numbers of child pages which themselves are other non-leaf/leaf nodes in the tree.
   */
	PageId pageNoArray[ INTARRAYNONLEAFSIZE + 1 ];
};


/**
 * @brief Structure for all leaf nodes when the key is of INTEGER type.
*/
struct LeafNodeInt{
  /**
   * Stores keys.
   */
	int keyArray[ INTARRAYLEAFSIZE ];

  /**
   * Stores RecordIds.
   */
	RecordId ridArray[ INTARRAYLEAFSIZE ];

  /**
   * Page number of the leaf on the right side.
	 * This linking of leaves allows to easily move from one leaf to the next leaf during index scan.
   */
	PageId rightSibPageNo;
};


/**
 * @brief BTreeIndex class. It implements a B+ Tree index on a single attribute of a
 * relation. This index supports only one scan at a time.
*/
class BTreeIndex {

 private:

  /**
   * File object for the index file.
   */
	File		*file;

  /**
   * Buffer Manager Instance.
   */
	BufMgr	*bufMgr;

  /**
   * Page number of meta page.
   */
	PageId	headerPageNum;

  /**
   * page number of root page of B+ tree inside index file.
   */
	PageId	rootPageNum;

  /**
   * Datatype of attribute over which index is built.
   */
	Datatype	attributeType;

  /**
   * Offset of attribute, over which index is built, inside records. 
   */
	int 		attrByteOffset;

  /**
   * Number of keys in leaf node, depending upon the type of key.
   */
	int			leafOccupancy;

  /**
   * Number of keys in non-leaf node, depending upon the type of key.
   */
	int			nodeOccupancy;


	// MEMBERS SPECIFIC TO SCANNING

  /**
   * True if an index scan has been started.
   */
	bool		scanExecuting;

  /**
   * Index of next entry to be scanned in current leaf being scanned.
   */
	int			nextEntry;

  /**
   * Page number of current page being scanned.
   */
	PageId	currentPageNum;

  /**
   * Current Page being scanned.
   */
	Page		*currentPageData;

  /**
   * Low INTEGER value for scan.
   */
	int			lowValInt;

  /**
   * Low DOUBLE value for scan.
   */
	double	lowValDouble;

  /**
   * Low STRING value for scan.
   */
	std::string	lowValString;

  /**
   * High INTEGER value for scan.
   */
	int			highValInt;

  /**
   * High DOUBLE value for scan.
   */
	double	highValDouble;

  /**
   * High STRING value for scan.
   */
	std::string highValString;
	
  /**
   * Low Operator. Can only be GT(>) or GTE(>=).
   */
	Operator	lowOp;

  /**
   * High Operator. Can only be LT(<) or LTE(<=).
   */
	Operator	highOp;

  /**
      * Set the index file of the btree
      * if the file exists, open it and return true
      * If it doesn't exist, open it and return false
      * @param relationName        Name of file.
      * @param outIndexName        Return the name of index file.
      * @param attrByteOffset                      Offset of attribute, over which index is to be built, in the record
      * @return whether file existed or not
  */
      bool setFile(const std::string & relationName, std::string & outIndexName, const int attrByteOffset);
      
  /**
      * Get the metapage of the B+ tree
      * @param bufMgrIn                                            Buffer Manager Instance
      * @param outIndexName        Return the name of index file.  
      * @param fileExists - whether or not the file exists
      * @return the meta page of the index file
  */	
  
      IndexMetaInfo *getMetaPage(BufMgr *bufMgrIn, bool fileExists, const std::string & relationName,  const int attrByteOffset,
                const Datatype attrType);
  /**
      * Scan each record in the data file
      * insert into B+ Tree
      * and place into bufMgr
      * @param relationName - the relation's name
      *
  */
      void scanDataFile(const std::string & relationName);

  /**
    *Creates initial root of the tree
  */  
      void createInitRoot();

  /**
    * Creates initial leaf node
    * @param leafNo - page number of leaf node 
  */
      void createInitLeaf(PageId &leafNo);

  /**
    * Prints out the leaf node
    * @param leaf - leaf node to print out
  */
      void printLeafNode(LeafNodeInt *leaf, PageId leafNo);

  /**
    * returns true if the leaf is full
    * false otherwise
    * @param leaf - the leaf node we're testing fullness for
    * @return true if full; false otherwise
  */
      bool leafFull(LeafNodeInt *leaf);

  /**
    * returns true if nonleafnode is full
    * false otherwise
    * @param nLeaf - non leaf node we're testing fullness for
    * @return true if full; false otherwise
  */
      bool nonLeafFull(NonLeafNodeInt *nLeaf);

  /**
    * returns the index of where to insert key into leaf
    * @param leaf - the leaf node we're inserting key into
    * @param key - key that we are inserting
    * @return index to insert
  */
     int findInsertPlaceLeaf(int *key, LeafNodeInt *leaf);
      
  /**
    * Inserts key into leaf
    * @param key - key that we are inserting
    * @param rid - record id that we are inserting
    * @param leaf - leaf node we are inserting key and rid into
  */
     void insertLeaf(int *key, const RecordId rid, LeafNodeInt *leaf);

  /**
    * returns the index of where to insert key into leaf
    * @param leaf - the leaf node we're inserting key into
    * @param key - key that we are inserting
    * @return index to insert
  */
     int findInsertPlaceNonLeaf(int *key, NonLeafNodeInt *nLeaf);

  /**
    * Inserts key into leaf
    * @param key - key that we are inserting
    * @param rid - record id that we are inserting
    * @param leaf - leaf node we are inserting key and rid into
  */
     void insertNonLeaf(int *key, PageId fLeafNo, PageId sLeafNo, NonLeafNodeInt *nLeaf);
     

  /**
    * retrieves the root leaf node when the root is the only node
    * @param getLeaf - pointer to the leaf node we retrieved
    * @param rootNode - root Node leaf is connected to
    * @return pageNo of the leaf node
  */
    PageId getInitLeaf(LeafNodeInt *getLeaf, NonLeafNodeInt *rootNode, Page*& realRoot);

  /**
    * prints out the whole dang tree when node = root
  */
     void printTree(NonLeafNodeInt* node);

  /**
    * prints out the nonleafNode
  */
     void printNonLeafNode(NonLeafNodeInt* node);

  /**
    * finds out where to split leaf
  */
    int findInsertPlaceSplit(int *keyArray, int *key, bool isLeaf);

    void splitRoot(PageId fNextNode, PageId sNextNode, NonLeafNodeInt *root, int *intKey, bool level);

    void splitNonLeafNode(PageId fNextNode, PageId sNextNode, PageId oldNode, NonLeafNodeInt *currNode, std::list <PageId> &path, bool firstLevel, int *intKey);

    void splitLeaf(LeafNodeInt *leaf, PageId oldLeaf, std::list <PageId> &path, int *key, RecordId rid);

    void findPath(std::list <PageId> &path, int *key);

    void updateScanVars(Page *currPage, PageId currPageNo, LeafNodeInt *leafNode, int indexStart);

 public:

  /**
   * BTreeIndex Constructor. 
	 * Check to see if the corresponding index file exists. If so, open the file.
	 * If not, create it and insert entries for every tuple in the /full
base relation using FileScan class.
   *
   * @param relationName        Name of file.
   * @param outIndexName        Return the name of index file.
   * @param bufMgrIn						Buffer Manager Instance
   * @param attrByteOffset			Offset of attribute, over which index is to be built, in the record
   * @param attrType						Datatype of attribute over which index is built
   * @throws  BadIndexInfoException     If the index file already exists for the corresponding attribute, but values in metapage(relationName, attribute byte offset, attribute type etc.) do not match with values received through constructor parameters.
   */
	BTreeIndex(const std::string & relationName, std::string & outIndexName,
						BufMgr *bufMgrIn,	const int attrByteOffset,	const Datatype attrType);
	

  /**
   * BTreeIndex Destructor. 
	 * End any initialized scan, flush index file, after unpinning any pinned pages, from the buffer manager
	 * and delete file instance thereby closing the index file.
	 * Destructor should not throw any exceptions. All exceptions should be caught in here itself. 
	 * */
	~BTreeIndex();


  /**
	 * Insert a new entry using the pair <value,rid>. 
	 * Start from root to recursively find out the leaf to insert the entry in. The insertion may cause splitting of leaf node.
	 * This splitting will require addition of new leaf page number entry into the parent non-leaf, which may in-turn get split.
	 * This may continue all the way upto the root causing the root to get split. If root gets split, metapage needs to be changed accordingly.
	 * Make sure to unpin pages as soon as you can.
   * @param key			Key to insert, pointer to integer/double/char string
   * @param rid			Record ID of a record whose entry is getting inserted into the index.
	**/
	const void insertEntry(const void* key, const RecordId rid);


  /**
	 * Begin a filtered scan of the index.  For instance, if the method is called 
	 * using ("a",GT,"d",LTE) then we should seek all entries with a value 
	 * greater than "a" and less than or equal to "d".
	 * If another scan is already executing, that needs to be ended here.
	 * Set up all the variables for scan. Start from root to find out the leaf page that contains the first RecordID
	 * that satisfies the scan parameters. Keep that page pinned in the buffer pool.
   * @param lowVal	Low value of range, pointer to integer / double / char string
   * @param lowOp		Low operator (GT/GTE)
   * @param highVal	High value of range, pointer to integer / double / char string
   * @param highOp	High operator (LT/LTE)
   * @throws  BadOpcodesException If lowOp and highOp do not contain one of their their expected values 
   * @throws  BadScanrangeException If lowVal > highval
	 * @throws  NoSuchKeyFoundException If there is no key in the B+ tree that satisfies the scan criteria.
	**/
	const void startScan(const void* lowVal, const Operator lowOp, const void* highVal, const Operator highOp);


  /**
	 * Fetch the record id of the next index entry that matches the scan.
	 * Return the next record from current page being scanned. If current page has been scanned to its entirety, move on to the right sibling of current page, if any exists, to start scanning that page. Make sure to unpin any pages that are no longer required.
   * @param outRid	RecordId of next record found that satisfies the scan criteria returned in this
	 * @throws ScanNotInitializedException If no scan has been initialized.
	 * @throws IndexScanCompletedException If no more records, satisfying the scan criteria, are left to be scanned.
	**/
	const void scanNext(RecordId& outRid);  // returned record id


  /**
	 * Terminate the current scan. Unpin any pinned pages. Reset scan specific variables.
	 * @throws ScanNotInitializedException If no scan has been initialized.
	**/
	const void endScan();
	
};

}
