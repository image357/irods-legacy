/**
 * @file  msiHelper.c
 *
 */

/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/
/* reHelper.c */

#include "msiHelper.h"
int
msiGetStdoutInExecCmdOut (msParam_t *inpExecCmdOut, msParam_t *outStr,
ruleExecInfo_t *rei)
{
    char *strPtr;

    rei->status = getStdoutInExecCmdOut (inpExecCmdOut, &strPtr);

    if (rei->status < 0) return rei->status;

    fillStrInMsParam (outStr, strPtr);

    return rei->status;
}

int
msiGetStderrInExecCmdOut (msParam_t *inpExecCmdOut, msParam_t *outStr,
ruleExecInfo_t *rei)
{
    char *strPtr;

    rei->status = getStderrInExecCmdOut (inpExecCmdOut, &strPtr);

    if (rei->status < 0) return rei->status;

    fillStrInMsParam (outStr, strPtr);

    return rei->status;
}

/**
 * \fn msiWriteRodsLog (msParam_t *inpParam1,  msParam_t *outParam, ruleExecInfo_t *rei)
 *
 * \brief Writes a message into the server rodsLog
 *
 * \module ERA
 *
 * \since 2.3
 *
 * \author  Jean-Yves Neif
 * \date    2009-06-15
 *
 * \remark Terrell Russell - msi documentation, 2009-12-17
 *
 * \note  This call should only be used through the rcExecMyRule (irule) call
 *        i.e., rule execution initiated by clients and should not be called
 *        internally by the server since it interacts with the client through
 *        the normal client/server socket connection.
 *
 * \usage None
 *
 * \param[in] inpParam1 - A STR_MS_T which specifies the message to log.
 * \param[out] outParam - An INT_MS_T containing the status.
 * \param[in,out] rei - The RuleExecInfo structure that is automatically
 *    handled by the rule engine. The user does not include rei as a
 *    parameter in the rule invocation.
 *
 * \DolVarDependence
 * \DolVarModified
 * \iCatAttrDependence
 * \iCatAttrModified
 * \sideeffect
 *
 * \return integer
 * \retval 0 upon success
 * \pre
 * \post
 * \sa
 * \bug  no known bugs
**/
int
msiWriteRodsLog (msParam_t *inpParam1,  msParam_t *outParam, ruleExecInfo_t *rei)
{
    rsComm_t *rsComm;

    RE_TEST_MACRO (" Calling msiWriteRodsLog")

    if (rei == NULL || rei->rsComm == NULL) {
        rodsLog (LOG_ERROR,
        "msiWriteRodsLog: input rei or rsComm is NULL");
        return (SYS_INTERNAL_NULL_INPUT_ERR);
    }

    rsComm = rei->rsComm;

    if ( inpParam1 == NULL ) {
        rodsLogAndErrorMsg (LOG_ERROR, &rsComm->rError, rei->status,
        "msiWriteRodsLog: input Param1 is NULL");
        rei->status = USER__NULL_INPUT_ERR;
        return (rei->status);
    }

    if (strcmp (inpParam1->type, STR_MS_T) == 0) {
        rodsLog(LOG_NOTICE,
          "msiWriteRodsLog message: %s", inpParam1->inOutStruct);
    } else {
        rodsLogAndErrorMsg (LOG_ERROR, &rsComm->rError, rei->status,
        "msiWriteRodsLog: Unsupported input Param1 types %s",
        inpParam1->type);
        rei->status = UNKNOWN_PARAM_IN_RULE_ERR;
        return (rei->status);
    }

    rei->status = 0;

    fillIntInMsParam (outParam, rei->status);

    return (rei->status);
}

int
msiAddKeyValToMspStr (msParam_t *keyStr, msParam_t *valStr, 
msParam_t *msKeyValStr, ruleExecInfo_t *rei)
{
    RE_TEST_MACRO (" Calling msiAddKeyValToMspStr")

    if (rei == NULL) {
        rodsLog (LOG_ERROR,
          "msiAddKeyValToMspStr: input rei is NULL");
        rei->status = SYS_INTERNAL_NULL_INPUT_ERR;
        return (rei->status);
    }

    rei->status = addKeyValToMspStr (keyStr, valStr, msKeyValStr);


    return rei->status;
}

int
msiSplitPath (msParam_t *inpPath,  msParam_t *outParentColl, 
msParam_t *outChildName, ruleExecInfo_t *rei)
{
    char parent[MAX_NAME_LEN], child[MAX_NAME_LEN];

    RE_TEST_MACRO (" Calling msiSplitPath")

    if (rei == NULL) {
        rodsLog (LOG_ERROR,
          "msiSplitPath: input rei is NULL");
        rei->status = SYS_INTERNAL_NULL_INPUT_ERR;
        return (rei->status);
    }

    if ( inpPath == NULL ) {
        rodsLog (LOG_ERROR,
          "msiSplitPath: input inpPath is NULL");
        rei->status = USER__NULL_INPUT_ERR;
        return (rei->status);
    }

    if (strcmp (inpPath->type, STR_MS_T) == 0) {
        if ((rei->status = splitPathByKey ((char *) inpPath->inOutStruct,
          parent, child, '/')) < 0) {
            rodsLog (LOG_ERROR,
              "msiSplitPath: splitPathByKey for %s error, status = %d",
              (char *) inpPath->inOutStruct, rei->status);
        } else {
	    fillStrInMsParam (outParentColl, parent);
	    fillStrInMsParam (outChildName, child);
	}
    } else {
        rodsLog (LOG_ERROR,
        "msiSplitPath: Unsupported input inpPath types %s",
        inpPath->type);
        rei->status = UNKNOWN_PARAM_IN_RULE_ERR;
    }
    return (rei->status);
}

/**
 * \fn msiGetSessionVarValue (msParam_t *inpVar,  msParam_t *outputMode, ruleExecInfo_t *rei)
 *
 * \brief Gets the value of a session variable in the rei
 *
 * \module 
 *
 * \since 2.3
 *
 * \author  Michael Wan
 * \date    2009-06-15
 *
 * \remark Terrell Russell - msi documentation, 2009-12-17
 *
 * \note  This call should only be used through the rcExecMyRule (irule) call
 *        i.e., rule execution initiated by clients and should not be called
 *        internally by the server since it interacts with the client through
 *        the normal client/server socket connection.
 *
 * \usage None
 *
 * \param[in] inpVar - A STR_MS_T which specifies the name of the session
 *             variable to output. The input session variable should NOT start
 *             with the "$" character. An input value of "all" means
 *             output all valid session variables. 
 * \param[in] outputMode - A STR_MS_T which specifies the output mode. Valid modes are:
 *      \li "server" - log the output to the server log
 *      \li "client" - send the output to the client in rError
 *      \li "all" - both client and server
 * \param[in,out] rei - The RuleExecInfo structure that is automatically
 *    handled by the rule engine. The user does not include rei as a
 *    parameter in the rule invocation.
 *
 * \DolVarDependence
 * \DolVarModified
 * \iCatAttrDependence
 * \iCatAttrModified
 * \sideeffect
 *
 * \return integer
 * \retval 0 upon success
 * \pre
 * \post
 * \sa
 * \bug  no known bugs
**/
int
msiGetSessionVarValue (msParam_t *inpVar,  msParam_t *outputMode, ruleExecInfo_t *rei)
{
    char *inpVarStr, *outputModeStr;
    char errMsg[ERR_MSG_LEN];
    rsComm_t *rsComm;

    RE_TEST_MACRO (" Calling msiGetSessionVarValue")

    if (rei == NULL || rei->rsComm == NULL) {
        rodsLog (LOG_ERROR,
          "msiGetSessionVar: input rei or rei->rsComm is NULL");
        rei->status = SYS_INTERNAL_NULL_INPUT_ERR;
        return (rei->status);
    }

    if (inpVar == NULL || outputMode == NULL) {
        rodsLog (LOG_ERROR,
          "msiGetSessionVarValue: input inpVar or outputMode is NULL");
        rei->status = USER__NULL_INPUT_ERR;
        return (rei->status);
    }

    if (strcmp (inpVar->type, STR_MS_T) != 0 || 
      strcmp (outputMode->type, STR_MS_T) != 0) {
        rodsLog (LOG_ERROR,
        "msiGetSessionVarValue: Unsupported *inpVar or outputMode type");
        rei->status = UNKNOWN_PARAM_IN_RULE_ERR;
	return (rei->status);
    }
    rsComm = rei->rsComm;
    inpVarStr = (char *) inpVar->inOutStruct;
    outputModeStr = (char *) outputMode->inOutStruct;

    if (inpVarStr == NULL || outputModeStr == NULL) {
        rodsLog (LOG_ERROR,
          "msiGetSessionVarValue: input inpVar or outputMode is NULL");
        rei->status = USER__NULL_INPUT_ERR;
        return (rei->status);
    }

    if (strcmp (inpVarStr, "all") == 0) {
	keyValPair_t varKeyVal;
	int i;
	bzero (&varKeyVal, sizeof (varKeyVal));
	rei->status = getAllSessionVarValue ("", rei, &varKeyVal);
	if (rei->status >= 0) {
            if (strcmp (outputModeStr, "server") == 0 ||
              strcmp (outputModeStr, "all") == 0) {
		for (i = 0; i < varKeyVal.len; i++) {
                    printf ("msiGetSessionVar: %s=%s\n", 
		      varKeyVal.keyWord[i], varKeyVal.value[i]);
		}
            }
            if (strcmp (outputModeStr, "client") == 0 ||
              strcmp (outputModeStr, "all") == 0) {
		for (i = 0; i < varKeyVal.len; i++) {
                    snprintf (errMsg, ERR_MSG_LEN,
                      "msiGetSessionVarValue: %s=%s\n", 
		        varKeyVal.keyWord[i], varKeyVal.value[i]);
                    addRErrorMsg (&rsComm->rError, 0, errMsg);
		}
	    }
	    clearKeyVal (&varKeyVal);
	}
    } else {
        char *outStr = NULL;
	rei->status = getSessionVarValue ("", inpVarStr, rei, &outStr);
	if (rei->status >= 0) {
	    if (strcmp (outputModeStr, "server") == 0 ||
	      strcmp (outputModeStr, "all") == 0) {
	        printf ("msiGetSessionVarValue: %s=%s\n", inpVarStr, outStr);
	    }
            if (strcmp (outputModeStr, "client") == 0 ||
              strcmp (outputModeStr, "all") == 0) {
                snprintf (errMsg, ERR_MSG_LEN, 
		  "msiGetSessionVarValue: %s=%s\n", inpVarStr, outStr);
		addRErrorMsg (&rsComm->rError, 0, errMsg);
            }
	}
	if (outStr != NULL) free (outStr);
    }
    return (rei->status);
}

