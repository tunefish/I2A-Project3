#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "stemmer.h"

#define _REPLACE_SUFFIX(__w, __s, __r) \
BEGIN_REPLACE_SUFFIX(__w, __s, __r) \
_END_REPLACE_OR

#define _REPLACE_SUFFIX_COND(__w, __s, __r, __c) \
_BEGIN_REPLACE_SUFFIX_COND(__w, __s, __r, __c) \
_END_REPLACE_OR

#define _BEGIN_REPLACE_SUFFIX(__w, __s, __r) \
if (ends_with(__w, __s)) { \
    __w = replace_suffix(__w, strlen(__s), __r);

#define _BEGIN_REPLACE_SUFFIX_COND(__w, __s, __r, __c) \
if (ends_with(__w, __s)) { \
    if (__c) { \
        __w = replace_suffix(__w, strlen(__s), __r); \
    _END_REPLACE_COND

#define _OR_REPLACE_SUFFIX(__w, __s, __r) \
} else _BEGIN_REPLACE_SUFFIX(__w, __s, __r)

#define _OR_REPLACE_SUFFIX_COND(__w, __s, __r, __c) \
_OR_REPLACE_SUFFIX_COND_NE(__w, __s, __r, __c) \
    _END_REPLACE_COND

#define _OR_REPLACE_SUFFIX_COND_NE(__w, __s, __r, __c) \
} else if (ends_with(__w, __s)) { \
    if (__c) { \
        __w = replace_suffix(__w, strlen(__s), __r);

#define _END_REPLACE_COND }
#define _END_REPLACE_OR }

int calculate_m(char *word, int suffix_len);
int is_consonant(char *word, int i);
int contains_vowel(char *word, int suffix_len);
int ends_with(char *word, char *suffix);
int ends_with_double_consonant(char *word);
int ends_with_cvc(char *word, int suffix_len);
char *replace_suffix(char *word, int suffix_length, char *replacement);

/*
 * Calculates m value for a word
 */
int calculate_m(char *word, int suffix_len) {
    if (strlen(word) <= suffix_len) {
        return 0;
    }
    
    int parts = 1;

    // type (consonant = 1, vowel = 0) of first letter
    char first_type = is_consonant(word, 0);

    // type of last checked letter
    char tmp_type = first_type;

    int i = 0;
    while (i < strlen(word) - suffix_len) {
        // if last checked type is different from current type, we enter a new part of the word
        if (tmp_type != is_consonant(word, i)) {
            tmp_type = !tmp_type;
            parts++;
        }
        i++;
    }

    //       remove optional initial consonant   remove optional vowel ending
    return (        parts - first_type         -    (parts - first_type)%2    ) / 2;
}

/*
 * Checkes whether i-th character of word is a consonant
 */
int is_consonant(char *word, int i) {
    char c = tolower(word[i]);
    return c != 'a' && c != 'e' && c != 'i' && c != 'o' & c != 'u' && !(c == 'y' && i > 0 && is_consonant(word, i-1));
}

/*
 * Checks whether word contains a vowel
 */
int contains_vowel(char *word, int suffix_len) {
    if (strlen(word) <= suffix_len) {
        return 0;
    }
    
    int i = 0;
    while (i < strlen(word) - suffix_len) {
        if (!is_consonant(word, i)) {
            return 1;
        }

        i++;
    }

    return 0;
}

/*
 * Checks wether a word ends with a specific suffix
 */
int ends_with(char *word, char *suffix) {
    return strlen(word) >= strlen(suffix) && !memcmp(word + strlen(word) - strlen(suffix), suffix, strlen(suffix));
}

/*
 * Checks whether word ends with a double consonant
 */
int ends_with_double_consonant(char *word) {
    int l = strlen(word);
    return word[l-1] == word[l-2] && is_consonant(word, l-1);
}

/*
 * Checks whether word ands with a consonant-vowel-consonant combination where the last consonant is not W, X or Y
 */
int ends_with_cvc(char *word, int suffix_len) {
    if (strlen(word) <= suffix_len) {
        return 0;
    }
    
    int l = strlen(word) - 1 - suffix_len;
    return is_consonant(word, l-2) && !is_consonant(word, l-1) && is_consonant(word, l) && word[l] != 'w' && word[l] != 'x' && word[l] != 'y';
}

/*
 * Replaces a suffix of a word
 */
char *replace_suffix(char *word, int suffix_length, char *replacement) {
    if (suffix_length < strlen(replacement)) {
        word = (char *) realloc(word, strlen(word) - suffix_length + strlen(replacement) + 1);
    }

    // copy whole replacement string, including \0 string terminator
    memcpy(word + strlen(word) - suffix_length, replacement, strlen(replacement) + 1);
    return word;
}

/*
 * Runs Porter Stemming Algorithm on a word
 */
char *stem(char *word) {
    // copy word to new memory location
    char *result = (char *) malloc(strlen(word) + 1);
    memcpy(result, word, strlen(word) + 1);

    /* STEP 1a */
    _BEGIN_REPLACE_SUFFIX(result, "sses", "ss")
    _OR_REPLACE_SUFFIX(result, "ies", "i")
    _OR_REPLACE_SUFFIX(result, "ss", "ss")
    _OR_REPLACE_SUFFIX(result, "s", "")
    _END_REPLACE_OR

    /* STEP 1b */
    int cont = 0;
    int vowel = contains_vowel(result, 3);
    _BEGIN_REPLACE_SUFFIX_COND(result, "eed", "ee", calculate_m(result, 3) > 0)
    _OR_REPLACE_SUFFIX_COND_NE(result, "ing", "", vowel)
        cont = 1;
    _END_REPLACE_COND
    _OR_REPLACE_SUFFIX_COND_NE(result, "ed", "", vowel || !is_consonant(result, strlen(result) - 3))
        cont = 1;
    _END_REPLACE_COND
    _END_REPLACE_OR

    int l = strlen(result) - 1;
    if (cont) {
        _BEGIN_REPLACE_SUFFIX(result, "at", "ate")
        _OR_REPLACE_SUFFIX(result, "bl", "ble")
        _OR_REPLACE_SUFFIX(result, "iz", "ize")
        _OR_REPLACE_SUFFIX_COND_NE(result, "", "", ends_with_double_consonant(result) && result[l] != 'l' && result[l] != 's' && result[l] != 'z')
            // removes the last consonant of the consonant pair at the end of the word
            result[l] = '\0';
        _END_REPLACE_COND
        _OR_REPLACE_SUFFIX_COND(result, "", "e", calculate_m(result, 0) == 1 && ends_with_cvc(result, 0))
        _END_REPLACE_OR
    }

    /* STEP 1c */
    _REPLACE_SUFFIX_COND(result, "y", "i", contains_vowel(result, 1))

    /* STEP 2 */
    int m7 = calculate_m(result, 7);
    int m6 = m7 > 0 ? m7 : calculate_m(result, 6);
    int m5 = m6 > 0 ? m6 : calculate_m(result, 5);
    int m4 = m5 > 0 ? m5 : calculate_m(result, 4);
    int m3 = m4 > 0 ? m4 : calculate_m(result, 3);
    _BEGIN_REPLACE_SUFFIX_COND(result, "ational", "ate", m7 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "tional", "tion", m6 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "enci", "ence", m4 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "anci", "ance", m4 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "izer", "ize", m4 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "abli", "able", m4 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "alli", "al", m4 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "entli", "ent", m5 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "eli", "e", m3 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "ousli", "ous", m5 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "ization", "ize", m7 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "ation", "ate", m5 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "ator", "ate", m4 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "alism", "al", m5 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "ivenes", "ive", m6 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "fulness", "ful", m7 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "ousness", "ous", m7 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "aliti", "al", m5 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "iviti", "ive", m5 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "biliti", "ble", m6 > 0)
    _END_REPLACE_OR

    /* STEP 3 */
    m5 = calculate_m(result, 5);
    m4 = m5 > 0 ? m5 : calculate_m(result, 4);
    m3 = m4 > 0 ? m4 : calculate_m(result, 3);
    _BEGIN_REPLACE_SUFFIX_COND(result, "icate", "ic", m5 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "ative", "", m5 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "alize", "al", m5 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "aciti", "ic", m5 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "ical", "ic", m4 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "ful", "", m3 > 0)
    _OR_REPLACE_SUFFIX_COND(result, "ness", "", m4 > 0)
    _END_REPLACE_OR

    /* STEP 4 */
    l = strlen(result) - 1;
    m5 = calculate_m(result, 5);
    m4 = m5 > 1 ? m5 : calculate_m(result, 4);
    m3 = m4 > 1 ? m4 : calculate_m(result, 3);
    int m2 = m3 > 1 ? m3 : calculate_m(result, 2);
    _BEGIN_REPLACE_SUFFIX_COND(result, "al", "", m2 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "ance", "", m4 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "ence", "", m4 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "er", "", m2 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "ic", "", m2 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "able", "", m4 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "ible", "", m4 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "ant", "", m3 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "ement", "", m5 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "ment", "", m4 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "ent", "", m3 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "ion", "", result[l-3] == 's' || result[l-3] == 't')
    _OR_REPLACE_SUFFIX_COND(result, "ou", "", m2 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "ism", "", m3 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "ate", "", m3 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "iti", "", m3 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "ous", "", m3 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "ive", "", m3 > 1)
    _OR_REPLACE_SUFFIX_COND(result, "ize", "", m3 > 1)
    _END_REPLACE_OR

    /* STEP 5a */
    int m1 = calculate_m(result, 1);
    _REPLACE_SUFFIX_COND(result, "e", "", m1 > 1 || (m1 == 1 && !ends_with_cvc(result, 1)))

    /* STEP 5b */
    _REPLACE_SUFFIX_COND(result, "l", "", calculate_m(result, 0) > 1 && ends_with_double_consonant(result))

    return result;
}
