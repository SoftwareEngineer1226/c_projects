#include "common.h"


//------------------------------------------------------------------------------
/*
   Default hash function
   def_hashfunc() is the default used by hashtable_ts_init() when the user didn't specify one.
   This is a simple/naive hash function which adds the key's ASCII char values. It will probably generate lots of collisions on large hash tables.
*/
static inline uint32_t def_hashfunc (const uint32_t keyP)
{
  return keyP % HASH_TABLE_SIZE;
}

void hashtable_ts_init (my_hash_table_t * hashtblP,
    uint32_t (*hashfuncP) (const uint32_t), char *tablename)
{
  int i;
  memset(hashtblP, 0, sizeof(*hashtblP));

  if (hashfuncP)
    hashtblP->hashfunc = hashfuncP;
  else
    hashtblP->hashfunc = def_hashfunc;
  
  for (i = 0; i < HASH_TABLE_SIZE; i++) {
    hashtblP->nodes[i]=NULL;
  }

  hashtblP->name = strdup(tablename);
}

//------------------------------------------------------------------------------
/*
   Adding a new element
   To make sure the hash value is not bigger than size, the result of the user provided hash function is used modulo size.
*/
hashtable_rc_t
hashtable_ts_insert (
  my_hash_table_t * hashtblP,
  uint32_t keyP,
  void *dataP)
{
  my_hash_node_t                     *node = NULL;
  uint32_t                             hash = 0;

  if (!hashtblP) {
    return HASH_TABLE_BAD_PARAMETER_HASHTABLE;
  }

  hash = hashtblP->hashfunc(keyP);
  node = hashtblP->nodes[hash];

  while (node) {
      if (node->key == keyP) {
      if ((node->data) && (node->data != dataP)) {
//        printf("%s, %d, free(node->data);\n", __FILE__, __LINE__);
        free(node->data);
        node->data = dataP;
        printf("%s(key 0x%x data %p) return INSERT_OVERWRITTEN_DATA\n", __FUNCTION__, keyP, dataP);
        return HASH_TABLE_INSERT_OVERWRITTEN_DATA;
      }
      node->data = dataP;
      //printf("%s(key 0x%x data %p) return HASH_TABLE_OK\n", __FUNCTION__, keyP, dataP);
      return HASH_TABLE_OK;
      }
    
    node = node->next;
  }
//  printf("%s, %d, node = malloc (sizeof (my_hash_node_t))\n", __FILE__, __LINE__);
  if (!(node = (my_hash_node_t*)malloc (sizeof (my_hash_node_t)))) {
    return HASH_TABLE_SYSTEM_ERROR;
  }

  node->key = keyP;
  node->data = dataP;

  if (hashtblP->nodes[hash]) {
    node->next = hashtblP->nodes[hash];
  } else {
    node->next = NULL;
  }

  hashtblP->nodes[hash] = node;
  hashtblP->num_elements++;
  //printf("%s(key 0x%x data %p) next %p return HASH_TABLE_OK\n", __FUNCTION__, keyP, dataP, node->next);
  return HASH_TABLE_OK;
}

//------------------------------------------------------------------------------
/*
   To free_wrapper an element from the hash table, we just search for it in the linked list for that hash value,
   and free_wrapper it if it is found. If it was not found, it is an error and -1 is returned.
*/
hashtable_rc_t
hashtable_ts_free (
  my_hash_table_t * hashtblP,
  const uint32_t keyP)
{
  my_hash_node_t                     *node,
                                         *prevnode = NULL;
  uint32_t                                 hash = 0;

  if (!hashtblP) {
    return HASH_TABLE_BAD_PARAMETER_HASHTABLE;
  }

  hash = hashtblP->hashfunc(keyP);
  node = hashtblP->nodes[hash];

  while (node) {
    if (node->key == keyP) {
      if (prevnode)
        prevnode->next = node->next;
      else
        hashtblP->nodes[hash] = node->next;

      if (node->data) {
//          printf("%s, %d, free(node->data);\n", __FILE__, __LINE__);
        free(node->data);
        node->data = NULL;
      }
//      printf("%s, %d, free(node);\n", __FILE__, __LINE__);
      free(node);
      node=NULL;
      hashtblP->num_elements--;
      //printf("%s(key 0x%x) return OK\n", __FUNCTION__, keyP);
      return HASH_TABLE_OK;
    }

    prevnode = node;
    node = node->next;
  }

   printf("%s(key 0x%x) return KEY_NOT_EXISTS\n", __FUNCTION__, keyP);
  return HASH_TABLE_KEY_NOT_EXISTS;
}

//------------------------------------------------------------------------------
/*
   Searching for an element is easy. We just search through the linked list for the corresponding hash value.
   NULL is returned if we didn't find it.
*/
hashtable_rc_t
hashtable_ts_get (
  my_hash_table_t * hashtblP,
  const uint32_t keyP,
  void **dataP)
{
  my_hash_node_t                 *node = NULL;
  uint32_t                             hash = 0;

  *dataP = NULL;
  if (!hashtblP) {
    return HASH_TABLE_BAD_PARAMETER_HASHTABLE;
  }

  hash = hashtblP->hashfunc(keyP);

  node = hashtblP->nodes[hash];

  while (node) {
    if (node->key == keyP) {
      *dataP = node->data;
      //printf("%s, key = %u, lock\n", hashtblP->name, keyP);
      //printf("%s(key 0x%x data %p) return OK\n", __FUNCTION__, keyP, *dataP);
      return HASH_TABLE_OK;
    }

    node = node->next;
  }
  //printf("%s(key 0x%x) return KEY_NOT_EXISTS\n", __FUNCTION__, keyP);

  return HASH_TABLE_KEY_NOT_EXISTS;
}
