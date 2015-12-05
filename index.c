#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>

#include "index.h"
#include "stemmer.h"
#include "util.h"

#define MAX_SEARCH_RESULTS 10

void write_index_to_file(index_p index);
void parse_file_for_index(index_p index, char *file);
void load_stopwords();
void release_stopwords();
int is_stopword(char *word);
int find_str(void *objs, int struct_len, char *str, int min, int max);
int find_int(void *objs, int struct_len, int i, int min, int max);

int cmp_doc_found_desc(const void *a, const void *b);

static int nr_stopwords = 0;
static char **stopwords = NULL;

typedef struct doc_found {
    int doc_id;             // document id
    double dist;            // euclidian distance to TF-IDF of the words in the queue
    unsigned long flag;     // if the n-th most significant bit is set, the n-th search term was found in this document (ignoring stopwords)
} doc_found_t, *doc_found_p;

/*
 * Loads stopwords array from the stopwords file
 */
void load_stopwords() {
    // open file or print error message
    FILE *sw_file = fopen("stopwords", "r");
    if (!sw_file) {
        printf("stopwords file not found.\nCan't remove stopwords!\n");
        return;
    }

    // count number of stopwords
    nr_stopwords = 0;
    char c;
    while ((c = getc(sw_file)) != EOF) {
        if (c == '\n') {
            nr_stopwords++;
        }
    }

    stopwords = (char**) malloc(sizeof(char *) * nr_stopwords);
    rewind(sw_file);

    // load stopwords into array
    int i;
    for (i = 0; i < nr_stopwords; i++) {
        stopwords[i] = read_line(sw_file);
    }

    fclose(sw_file);
}

/*
 * Releases the memory allocated for the stopwords array
 */
void release_stopwords() {
    if (stopwords) {
        int i;
        for (i = 0; i < nr_stopwords; i++) {
            free(stopwords[i]);
        }
        free(stopwords);
    }
}

/*
 * Checks whether a word is a stopwords
 */
int is_stopword(char *word) {
    if (!stopwords) {
        load_stopwords();
    }
    
    return find_str(stopwords, sizeof(char *), word, 0, nr_stopwords - 1) != -1;
}

/*
 * Adds a file to the index
 */
index_p add_file(index_p index, char *file) {
    // check if file exists and can be read
    FILE *f = fopen(file, "r");
    if (!f) {
        printf("Cannot open %s!\nIndex not updated.\n", file);
        return index;
    }
    fclose(f);
    
    // insert file into file list (alphabetically ordered)
    int doc_id = 0;
    
    // always insert temporary search document in the beginning
    if (strcmp(file, "._tmp_search_doc")) {
        for (doc_id = 0; doc_id < index->nr_docs; doc_id++) {
            int cmp = strcmp(index->documents[doc_id].name, file);

            if (!cmp) {
                printf("%s is already in the filebase.\n", file);
                return index;
            } else if (0 < cmp) {
                // right position in list found
                break;
            }
        }
    }
    
    // insert document in list
    index = (index_p) realloc(index, sizeof(index_t) + sizeof(indexed_document_t) * (index->nr_docs + 1));
    memmove(&index->documents[doc_id+1], &index->documents[doc_id], sizeof(indexed_document_t) * (index->nr_docs - doc_id));
    
    index->documents[doc_id].name = (char *) malloc(strlen(file) + 1);
    memcpy(index->documents[doc_id].name, file, strlen(file) + 1);
    index->documents[doc_id].nr_words = 0;
    index->nr_docs++;

    // update indices: increase indices which are greater or equal to doc_id of added document
    indexed_word_p w = index->words;
    while (w) {
        int i;
        for (i = 0; i < w->nr_docs; i++) {
            if (w->documents[i].id >= doc_id) {
                w->documents[i].id++;
            }
        }

        w = w->next;
    }

    // parse file contents and add words to index
    parse_file_for_index(index, file);
    write_index_to_file(index);
    return index;
}

/*
 * Removes a file from index
 */
void remove_file(index_p index, int doc_id) {
    // open file or print error message
    if (!index->nr_docs) {
        printf("Filebase empty!\n");
        return;
    }
    
    if (doc_id < 0 || doc_id >= index->nr_docs) {
        printf("Error: illegal document id. No document removed!\n");
    }

    // remove document from list in index
    free(index->documents[doc_id].name);
    memmove(&index->documents[doc_id], &index->documents[doc_id+1], sizeof(indexed_document_t) * (index->nr_docs - 1 - doc_id));
    index->nr_docs--;

    indexed_word_p w = index->words;    // current word
    indexed_word_p p = NULL;            // previous word
    
    // remove document from the list of each indexed word
    while (w) {
        // find index of removed document in list (or of first document with higher id)
        int i;
        int remove = 0;
        for (i = 0; i < w->nr_docs; i++) {
            if (w->documents[i].id == doc_id) {
                w->nr_docs--;
                // document found in list, indicate removal
                remove = 1;
                break;
            } else if (w->documents[i].id > doc_id) {
                break;
            }
        }
        
        // reduce document id of all documents with id > removed document id
        // and shift array items (in order to remove entry of the document we want to remove) if neccessary
        for (; i < w->nr_docs; i++) {
            w->documents[i] = w->documents[i+remove];
            w->documents[i].id--;
        }

        if (w->nr_docs == 0) {
            // only occurance of this word is in removed document -> remove word from index
            if (!p) {
                index->words = w->next;
            } else {
                p->next = w->next;
            }
            
            index->nr_words--;

            indexed_word_p n = w->next;
            free(w->stem);
            free(w);
            w = n;
        } else {
            // get next indexed word
            p = w;
            w = w->next;
        }
    }

    // commit changes to file
    write_index_to_file(index);
}

/*
 * Searches index for indexed words and returns documents containing these words
 */
index_p search_index(index_p *in, char *query) {
    nonalpha_to_space(query);
    
    FILE *search_file = fopen("._tmp_search_doc", "w");
    if (!search_file) {
        printf("Error: couldn't create temporary file to write.\nUnable to search\n");
        return NULL;
    }
    
    fprintf(search_file, "%s\n", query);
    fclose(search_file);
    
    *in = add_file(*in, "._tmp_search_doc");
    index_p index = *in;
    
    // compute TF-IDF vector for search document
    double *q_tfidf = (double *) malloc(sizeof(double) * index->nr_words);
    
    // threshold for the search, based on the distance of the search term to the empty document
    double euclid_threshold = 0;

    // document offset for each word where we need to continue searching (make use of sorted document ids)
    int *w_offset = (int *) malloc(sizeof(int) * index->nr_words);
    
    // calculate TF-IDF, threshold and offsets
    int wid = 0;
    indexed_word_p w = index->words;
    while (w) {
        if (!w->documents[0].id) {
            q_tfidf[wid] = w->documents[0].tf * logf(index->nr_docs / w->nr_docs);
            euclid_threshold += q_tfidf[wid] * q_tfidf[wid];
            w_offset[wid] = 1;
        } else {
            q_tfidf[wid] = 0;
            w_offset[wid] = 0;
        }
        
        w = w->next;
        wid++;
    }
    
    // threshold is the euclidian distance to the empty document
    euclid_threshold = sqrt(euclid_threshold);

    // euclidian distance of all documents to the search term (based on TF-IDF)
    doc_found_p euclid_dist = (doc_found_p) malloc(sizeof(doc_found_t) * index->nr_docs);
    memset(euclid_dist, 0, sizeof(doc_found_t) * index->nr_docs);
    
    // array of all search terms without stopwords
    char *words[index->documents[0].nr_words];
    memset(words, 0, sizeof(char *) * index->documents[0].nr_words);
    
    // index of last processed word in the search term
    int qid = 0;
    
    // compute euclidian distance for all documents; ignore temporary search document at index 0
    int d;
    int nr_results = 0;
    for (d = 1; d < index->nr_docs; d++) {
        euclid_dist[nr_results].dist = 0;
        euclid_dist[nr_results].flag = 0;
        euclid_dist[nr_results].doc_id = d;
        
        // compute TF-IDF for all words and sum the difference to the TF-IDF of the queue in euclid_dist
        qid = 0;
        int wid = 0;
        w = index->words;
        while (w) {
            int i = w_offset[wid];
            if (i < w->nr_docs && w->documents[i].id == d) {
                // word occurs in document -> calculate TF-IDF and subtract TF-IDF of queue; then square
                euclid_dist[nr_results].dist += pow(w->documents[i].tf * logf(index->nr_docs / w->nr_docs) - q_tfidf[wid], 2);
                
                // update bit mask (set qid-th most significant bit to 1)
                if (!w->documents[0].id) {
                    euclid_dist[nr_results].flag |= 1 << (sizeof(unsigned long) * 8 - 1 - qid);
                }
                
                w_offset[wid]++;
            } else {
                // word doesn't occur in document -> TF-IDF = 0 -> just square TF-IDF of queue
                euclid_dist[nr_results].dist += q_tfidf[wid] * q_tfidf[wid];
            }
            
            if (!w->documents[0].id) {
                // this word is part of the search term
                if (!words[qid]) {
                    words[qid] = w->stem;
                }
                qid++;
            }
            
            w = w->next;
            wid++;
        }
        
        euclid_dist[nr_results].dist = sqrtf(euclid_dist[nr_results].dist);
        
        // overwrite documents above threshold or without any hits in next iteration
        if (euclid_dist[nr_results].flag && euclid_dist[nr_results].dist < euclid_threshold) {
            nr_results++;
        }
    }
    
    free(q_tfidf);
    free(w_offset);
    
    // sort documents by euclidian distance to query
    qsort(euclid_dist, nr_results, sizeof(doc_found_t), cmp_doc_found_desc);
    
    // create result index
    index_p result = (index_p) malloc(sizeof(index_t) + sizeof(indexed_document_t) * MAX_SEARCH_RESULTS);
    result->nr_docs = 0;
    result->nr_words = 0;
    result->words = NULL;
    
    unsigned long last_flag = 0;    // flag of last processed document
    w = NULL;                       // current group of documents (of the same (sub-)set of search terms)
    indexed_word_p p = NULL;        // previous group of documents
    
    // create a index_p struct with the results, each 'word' in this index represents a group of documents which contains the same (sub-)set of search terms
    int i;
    for (i = 0; i <= MAX_SEARCH_RESULTS && i < nr_results; i++) {
        if (euclid_dist[i].flag != last_flag || i == 0) {
            // the flag is not equal to previous one => create new 'group' of documents
            indexed_word_p w_new = (indexed_word_p) malloc(sizeof(indexed_word_t));
            w_new->next = NULL;
            w_new->nr_docs = 0;
            w_new->stem = (char *) malloc(1);
            *w_new->stem = '\0';
            
            // create a string of all search terms found in this document
            int k;
            for (k = 0; k < index->documents[0].nr_words; k++) {
                // check whether k-th most significant bit is set
                if (euclid_dist[i].flag & (1 << (sizeof(unsigned long) * 8 - 1 - k))) {
                    w_new->stem = (char *) realloc(w_new->stem, strlen(w_new->stem) + strlen(words[k]) + 3);
                    strcat(w_new->stem, words[k]);
                    strcat(w_new->stem, ", ");
                }
            }
            
            // remove final ', '
            *(w_new->stem + strlen(w_new->stem) - 1) = '\0';
            *(w_new->stem + strlen(w_new->stem) - 1) = '\0';
            
            // update pointer to this group
            if (!w) {
                // first result document: set as first element of linked list
                result->words = w_new;
            } else {
                w->next = w_new;
            }
            
            p = w;
            w = w_new;
            last_flag = euclid_dist[i].flag;
            result->nr_words++;
        }
        
        // add document to group
        w = (indexed_word_p) realloc(w, sizeof(indexed_word_t) + sizeof(doc_t) * (w->nr_docs + 1));
        w->documents[w->nr_docs].id = i;
        w->documents[w->nr_docs].tf = 0;
        w->nr_docs++;
        
        // copy name of the document into result index
        char *d = index->documents[euclid_dist[i].doc_id].name;
        result->documents[i].name = (char *) malloc(strlen(d) + 10);
        sprintf(result->documents[i].name, "%08.5f %s", euclid_dist[i].dist, d);
        result->nr_docs++;
        
        // update pointer to this group (needed after realloc)
        if (!p) {
            result->words = w;
        } else {
            p->next = w;
        }
    }
    
    free(euclid_dist);
    
    remove_file(index, 0);
    remove("._tmp_search_doc");
    
    return result;
}

/*
 * Compares two doc_found structs based on euclidian distance to the search term (1st priority) and the document id (2nd priority; which is the same as comparing the names)
 */
int cmp_doc_found_desc(const void *a, const void *b) {
   doc_found_p aa = (doc_found_p) a;
   doc_found_p bb = (doc_found_p) b;
   
   if (aa->dist == bb->dist) {
       return (aa->doc_id < bb->doc_id) ? -1 : (aa->doc_id > bb->doc_id);
   } else {
       return (aa->dist < bb->dist) ? -1 : (aa->dist > bb->dist);
   }
}

/*
 * Regenerates the index based on the files in the filebase
 */
void rebuild_index(index_p index) {
    // clear index but keep filebase
    indexed_word_p w;
    while ((w = index->words)) {
        index->words = w->next;

        free(w->stem);
        free(w);
    }

    index->nr_words = 0;
    
    // rescan every document
    int i;
    for (i = 0; i < index->nr_docs; i++) {
        index->documents[i].nr_words = 0;
        parse_file_for_index(index, index->documents[i].name);
    }

    // save
    write_index_to_file(index);
}

/*
 * Parses a file and adds its words to the index
 */
void parse_file_for_index(index_p index, char *file) {
    // open file or print error message
    FILE *f = fopen(file, "r");
    if (!f) {
        printf("Cannot open %s!\nIndex not updated.\n", file);
        return;
    }

    // document id = index of document in list of all documents in filebase (alphabetically ordered)
    int doc_id = find_str(&index->documents[0].name, sizeof(indexed_document_t), file, 0, index->nr_docs-1);
    
    if (doc_id < 0) {
        printf("Error: %s is not in the filebase!\n", file);
        return;
    }

    char *l;
    while ((l = read_line(f))) {
        // turn non alpha characters into spaces
        nonalpha_to_space(l);

        char *word = strtok(l, " ");
        while (word) {
            // ignore stopwords
            if (is_stopword(word)) {
                word = strtok(NULL, " ");
                continue;
            }

            char *word_stem = stem(word);
            
            if (!strlen(word_stem)) {
                word = strtok(NULL, " ");
                continue;
            }

            // insert document into index / add new stem to index
            indexed_word_p w = index->words;    // current word
            indexed_word_p p = NULL;            // previous word
            int flag = 0;
            while (w && !flag) {
                int cmp = strcmp(w->stem, word_stem);
                if (!cmp) {
                    // stem is already indexed
                    flag = 1;
                    break;
                } else if (0 < cmp) {
                    // stem not indexed yet
                    flag = 2;
                    break;
                }

                p = w;
                w = w->next;
            }

            if (flag == 1) {
                // stem indexed, add document to list
                int i;
                for (i = 0; i < w->nr_docs; i++) {
                    if (w->documents[i].id == doc_id) {
                        // document is already indexed for this stem
                        flag = 0;
                        break;
                    } else if (w->documents[i].id > doc_id) {
                        break;
                    }
                }
                
                // only add document to list if it's not already in the list
                if (flag) {
                    w = (indexed_word_p) realloc(w, sizeof(indexed_word_t) + sizeof(doc_t) * (w->nr_docs + 1));
                    
                    // update pointer to this group (needed after realloc)
                    if (!p) {
                        index->words = w;
                    } else {
                        p->next = w;
                    }

                    // insert document in list
                    memmove(&w->documents[i+1], &w->documents[i], sizeof(doc_t) * (w->nr_docs - i));
                    w->documents[i].id = doc_id;
                    w->documents[i].tf = 1;
                    w->nr_docs++;
                } else {
                    // increase counter for number of occurances of this word in this document
                    w->documents[i].tf++;
                }
                
                free(word_stem);
            } else {
                // stem is not indexed, add it to index
                w = (indexed_word_p) malloc(sizeof(indexed_word_t) + sizeof(doc_t));
                w->stem = word_stem;
                w->nr_docs = 1;
                w->documents[0].id = doc_id;
                w->documents[0].tf = 1;
                
                index->nr_words++;

                // insert this word in linked list
                if (!p) {
                    w->next = index->words;
                    index->words = w;
                } else {
                    w->next = p->next;
                    p->next = w;
                }
            }
            
            // increase counter for total number of words in this document
            index->documents[doc_id].nr_words++;

            // get next word
            word = strtok(NULL, " ");
        }

        free(l);
    }

    fclose(f);
    
    
    // finalize computation of TF
    indexed_word_p w = index->words;
    while (w) {
        
        int i = find_int(&w->documents[0].id, sizeof(doc_t), doc_id, 0, w->nr_docs - 1);
        
        if (i >= 0) {
            w->documents[i].tf /= index->documents[doc_id].nr_words;
        }
        
        w = w->next;
    }
}

/*
 * Writes index to file
 */
void write_index_to_file(index_p index) {
    // STEP 1: write filebase to file
    FILE *fb_file = fopen("filebase", "w");
    if (!fb_file) {
        printf("Error: couldn't open filebase file to write.\nUnable to write index to file\n");
        return;
    }

    // each line contains the name (relative path) to one document in the filebase and the number of words in this document
    // format: <path/to/file>|<nr_words>
    int i;
    for (i = 0; i < index->nr_docs; i++) {
        fprintf(fb_file, "%s|%d\n", index->documents[i].name, index->documents[i].nr_words);
    }

    fclose(fb_file);

    // STEP 2: write index to file
    FILE *index_file = fopen("index", "w");
    if (!index_file) {
        printf("Error: couldn't open index file to write.\nUnable to write index to file\n");
        return;
    }

    // write one word in each line
    // format: <stem>:<n>:doc_id_1/<tf_stem_1>|doc_id_2/tf_stem_2>|..|doc_id_n/<tf_stem_n>
    indexed_word_p w = index->words;
    while (w) {
        fprintf(index_file, "%s:%i:%i/%f", w->stem, w->nr_docs, w->documents[0].id, w->documents[0].tf);

        // list all documents containing this word (or variations of it)
        int i;
        for(i = 1; i < w->nr_docs; i++) {
            fprintf(index_file, "|%i/%f", w->documents[i].id, w->documents[i].tf);
        }

        fprintf(index_file, "\n");

        w = w->next;
    }

    fclose(index_file);
}

/*
 * Parses and loads contents of the index file into a index struct
 */
index_p load_index() {
    // create index struct
    index_p index = (index_p) malloc(sizeof(index_t));
    index->words = NULL;
    index->nr_docs = 0;
    index->nr_words = 0;
    
    // STEP 1: populate list of all documents
    FILE *fb_file = fopen("filebase", "r");
    if (!fb_file) {
        printf("Error: filebase file not found.\nIndex not loaded!\n");
        return index;
    }

    // count total number of documents
    char c;
    int nr_docs = 0;
    while ((c = fgetc(fb_file)) != EOF) {
        if (c == '\n') {
            nr_docs++;
        }
    }

    rewind(fb_file);

    // create index struct
    index = (index_p) realloc(index, sizeof(index_t) + sizeof(indexed_document_t) * nr_docs);

    // load all documents in a list
    int i;
    for (i = 0; i < nr_docs; i++) {
        char *line = read_line(fb_file);
        
        // copy name to index
        char *tmp;
        char *doc = strtok(line, "|");
        index->documents[i].name = malloc(sizeof(char) * strlen(doc) + 1);
        memcpy(index->documents[i].name, doc, strlen(doc) + 1);
        
        // copy number of words to index
        doc = strtok(NULL, "|");
        index->documents[i].nr_words = strtol(doc, &tmp, 10);
        index->nr_docs++;
        
        free(line);
    }

    fclose(fb_file);

    // STEP 2: populate list of all words
    FILE * index_file = fopen("index", "r");
    if (!index_file) {
        printf("Error: index file not found.\nIndex not loaded!\n");
        return index;
    }

    indexed_word_p p = NULL;
    char *line, *stem, *docs, *doc, *tmp;
    while ((line = read_line(index_file))) {
        // get the stem
        stem = strtok(line, ":");

        // ignore empty lines
        if (!stem) {
            continue;
        }

        // get number of documents for this word
        int nr_docs = strtol(strtok(NULL, ":"), &tmp, 10);

        // create struct for stem
        indexed_word_p w = (indexed_word_p) malloc(sizeof(indexed_word_t) + sizeof(doc_t) * nr_docs);
        w->stem = (char *) malloc(sizeof(char) * strlen(stem) + 1);
        memcpy(w->stem, stem, strlen(stem) + 1);
        w->nr_docs = nr_docs;

        // insert into index
        if (!p) {
            index->words = w;
            w->next = NULL;
        } else {
            w->next = p->next;
            p->next = w;
        }
        p = w;

        // get list of documents containing this stem
        docs = strtok(NULL, ":");
        
        // read each document
        doc = strtok(docs, "|");

        int i = 0;
        while(doc != NULL) {
            sscanf(doc, "%i/%lf", &w->documents[i].id, &w->documents[i].tf);
            
            // get next document
            doc = strtok(NULL, "|");
            i++;
        }

        index->nr_words++;
        free(line);
    }

    fclose(index_file);

	return index;
}

/*
 * Frees the memory occupied by a index struct
 */
void close_index(index_p index) {
    indexed_word_p w;

    int i;
    for (i = 0; i < index->nr_docs; i++) {
        free(index->documents[i].name);
    }

    while ((w = index->words)) {
        index->words = w->next;

        free(w->stem);
        free(w);
    }
    free(index);
}

/*
 * Binary search an array for a string
 *  obj: pointer to an array of pointers to strings
         or array of fixed length structs containing a pointer to a string (in this case the obj pointer should point to the string pointer in the first struct)
 *  struct_len: length of one object in the array in bytes
 *  str: search string
 *  min: minimal index to search
 *  max: maximal index to search
 */
int find_str(void *objs, int struct_len, char *str, int min, int max) {
    int middle = (min + max) / 2;
    int cmp = strcmp(*((char **) (((char *) objs) + middle * struct_len)), str);

    if (!cmp) {
        // string found
        return middle;
    } else if (min != max) {
        if (cmp > 0 && min != middle) {
            // continue search in left half
            return find_str(objs, struct_len, str, min, middle - 1);
        } else if (max != middle) {
            // continue search in right half
            return find_str(objs, struct_len, str, middle + 1, max);
        }
    }
    
    // finished searching
    return -1;
}

/*
 * Binary search an array for an integer
 *  obj: pointer to an array of integers
         or array of fixed length structs containing an integer (in this case the obj pointer should point to the integer in the first struct)
 *  struct_len: length of one object in the array in bytes
 *  i: search integer
 *  min: minimal index to search
 *  max: maximal index to search
 */
int find_int(void *objs, int struct_len, int i, int min, int max) {
    int middle = (min + max) / 2;
    int cmp_val = *((int *) (((char *) objs) + middle * struct_len));

    if (cmp_val == i) {
        // string found
        return middle;
    } else if (min != max) {
        if (cmp_val > i && min != middle) {
            // continue search in left half
            return find_int(objs, struct_len, i, min, middle - 1);
        } else if (max != middle) {
            // continue search in right half
            return find_int(objs, struct_len, i, middle + 1, max);
        }
    }
    
    // finished searching
    return -1;
}
