/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/

/* ooiCi.c - OOI CI routines
 */
#include "ooiCi.h"
#include "msParam.h"

/* dictSetAttr - set a key/value pair. For non array, arrLen = 0 */ 
int
dictSetAttr (dictionary_t *dictionary, char *key, char *type_PI, void *valptr,
int arrLen)
{
    /* key and type_PI are replicated, but valptr is stolen */
    char **newKey;
    dictValue_t *newValue;
    int newLen;
    int i;

    if (dictionary == NULL) {
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    /* check if the keyword exists */

    for (i = 0; i < dictionary->len; i++) {
        if (strcmp (key, dictionary->key[i]) == 0) {
            free ( dictionary->value[i].ptr);
            dictionary->value[i].ptr = valptr;
            rstrcpy (dictionary->value[i].type_PI, type_PI, NAME_LEN);
            dictionary->value[i].len = arrLen;
            return (0);
        }
    }

    if ((dictionary->len % PTR_ARRAY_MALLOC_LEN) == 0) {
        newLen = dictionary->len + PTR_ARRAY_MALLOC_LEN;
        newKey = (char **) calloc (newLen, sizeof (newKey));
        newValue = (dictValue_t *) calloc (newLen,  sizeof (dictValue_t));
        for (i = 0; i < dictionary->len; i++) {
            newKey[i] = dictionary->key[i];
            newValue[i] = dictionary->value[i];
        }
        if (dictionary->key != NULL)
            free (dictionary->key);
        if (dictionary->value != NULL)
            free (dictionary->value);
        dictionary->key = newKey;
        dictionary->value = newValue;
    }

    dictionary->key[dictionary->len] = strdup (key);
    dictionary->value[dictionary->len].ptr = valptr;
    rstrcpy (dictionary->value[dictionary->len].type_PI, type_PI, NAME_LEN);
    dictionary->value[dictionary->len].len = arrLen;
    dictionary->len++;

    return (0);
}

dictValue_t *
dictGetAttr (dictionary_t *dictionary, char *key)
{
    int i;

    if (dictionary == NULL) {
        return (NULL);
    }

    for (i = 0; i < dictionary->len; i++) {
        if (strcmp (dictionary->key[i], key) == 0) {
            return (&dictionary->value[i]);
        }
    }
    return (NULL);
}

int
dictDelAttr (dictionary_t *dictionary, char *key) 
{
    int i, j;

    if (dictionary == NULL) {
        return (0);
    }

    for (i = 0; i < dictionary->len; i++) {
        if (dictionary->key[i] != NULL &&
          strcmp (dictionary->key[i], key) == 0) {
            free (dictionary->key[i]);
	    if (dictionary->value[i].ptr != NULL) {
                free (dictionary->value[i].ptr);
		dictionary->value[i].ptr = NULL;
            }
            dictionary->len--;
            for (j = i; j < dictionary->len; j++) {
                dictionary->key[j] = dictionary->key[j + 1];
                dictionary->value[j] = dictionary->value[j + 1];
            }
            if (dictionary->len <= 0) {
                free (dictionary->key);
                free (dictionary->value);
                dictionary->value = NULL;
                dictionary->key = NULL;
            }
            break;
        }
    }
    return (0);
}

int
clearDictionary (dictionary_t *dictionary)
{
    int i;

    if (dictionary == NULL || dictionary->len <= 0)
        return (0);

    for (i = 0; i < dictionary->len; i++) {
        free (dictionary->key[i]);
        free (dictionary->value[i].ptr);
    }

    free (dictionary->key);
    free (dictionary->value);
    bzero (dictionary, sizeof (keyValPair_t));
    return(0);
}

int
jsonPackDictionary (dictionary_t *dictionary, json_t **outObj)
{
    json_t *paramObj;
    int i, status;

    if (dictionary == NULL || outObj == NULL) return USER__NULL_INPUT_ERR;

    paramObj = json_object ();

    for (i = 0; i < dictionary->len; i++) {
	char *type_PI = dictionary->value[i].type_PI;

        if (strcmp (type_PI, STR_MS_T) == 0) {
            status = json_object_set_new (paramObj, dictionary->key[i],
              json_string ((char *) dictionary->value[i].ptr));
        } else if (strcmp (type_PI, INT_MS_T) == 0) {
#if JSON_INTEGER_IS_LONG_LONG
            rodsLong_t myInt = *(int *) dictionary->value[i].ptr;
#else
            int myInt = *(int *) dictionary->value[i].ptr;
#endif
            status = json_object_set_new (paramObj, dictionary->key[i],
              json_integer (myInt));
        } else if (strcmp (type_PI, FLOAT_MS_T) == 0) {
#if JSON_INTEGER_IS_LONG_LONG
            double myFloat = *(float *) dictionary->value[i].ptr;
#else
            float myFloat = *(float *) dictionary->value[i].ptr;
#endif
            status = json_object_set_new (paramObj, dictionary->key[i],
              json_real (myFloat));
        } else if (strcmp (type_PI, DOUBLE_MS_T) == 0) {
            /* DOUBLE_MS_T in iRODS is longlong */
#if JSON_INTEGER_IS_LONG_LONG
            rodsLong_t myInt = *(rodsLong_t *) dictionary->value[i].ptr;
#else
            int myInt = *(rodsLong_t *) dictionary->value[i].ptr;
#endif
            status = json_object_set_new (paramObj, dictionary->key[i],
              json_integer (myInt));
        } else if (strcmp (type_PI, BOOL_MS_T) == 0) {
            int myInt = *(int *) dictionary->value[i].ptr;
	    if (myInt == 0) {
                status = json_object_set_new (paramObj, dictionary->key[i],
                  json_false ());
            } else {
                status = json_object_set_new (paramObj, dictionary->key[i],
                  json_true ());
            }
        } else {
            rodsLog (LOG_ERROR, 
              "jsonPackDictionary: type_PI %s not supported", type_PI);
            json_decref (paramObj);
            return OOI_DICT_TYPE_NOT_SUPPORTED;
        }
        if (status != 0) {
            rodsLog (LOG_ERROR, 
              "jsonPackDictionary: son_object_set paramObj error");
            json_decref (paramObj);
            return OOI_JSON_OBJ_SET_ERR;
	}
    }
    *outObj = paramObj;

    return 0;
}

int
jsonPackOoiServReq (char *servName, char *servOpr, dictionary_t *params,
char **outStr)
{
    json_t *paramObj, *obj; 
    int status;

    if (servName == NULL || servOpr == NULL || params == NULL ||
      outStr == NULL) return USER__NULL_INPUT_ERR;

    status = jsonPackDictionary (params, &paramObj);

    if (status < 0) return status;

    obj = json_pack ("{s:{s:s,s:s,s:o}}",
                          SERVICE_REQUEST_STR,
                          SERVICE_NAME_STR, servName,
                          SERVICE_OP_STR, servOpr,
                          "params", paramObj);

    if (obj == NULL) {
        rodsLog (LOG_ERROR, "jsonPackOoiServReq: json_pack error");
        return OOI_JSON_PACK_ERR;
    }
    *outStr = json_dumps (obj, 0);
    json_decref (obj);
    if (*outStr == NULL) {
        rodsLog (LOG_ERROR, "jsonPackOoiServReq: json_dumps error");
        return OOI_JSON_DUMP_ERR;
    }
    return 0;
}

int
jsonPackOoiServReqForPost (char *servName, char *servOpr, dictionary_t *params,
char **outStr)
{
    char *tmpOutStr = NULL;
    int status, len;

    status = jsonPackOoiServReq (servName, servOpr, params, &tmpOutStr);

    if (status < 0) return status;

    len = strlen (tmpOutStr) + 20;
    *outStr = (char *)malloc (len);
    snprintf (*outStr, len, "payload=%s", tmpOutStr);
    free (tmpOutStr);
    return 0;
}
