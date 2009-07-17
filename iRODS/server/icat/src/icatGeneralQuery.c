/*** Copyright (c), The Regents of the University of California            ***
 *** For more information please refer to files in the COPYRIGHT directory ***/
/*

 These routines are the genSql routines, which are used to convert the
 generalQuery arguments into SQL select strings.  The generalQuery
 arguments are any arbitrary set of columns in the various tables, so
 these routines have to generate SQL that can link any table.column to
 any other.

 Also see the fklinks.c routine which calls fklink (below) to
 initialize the table table.

 At the core, is an algorithm to find a spanning tree in our graph set
 up by fklink.  This does not need to find the minimal spanning tree,
 just THE spanning tree, as there should be only one.  Thus there are
 no weights on the arcs of this tree either.  But complicating this is
 the fact that there are nodes that can create cycles in the
 semi-tree, but these are flagged so the code can stop when
 encountering these.

 There is also a routine that checks for cycles, tCycleChk, which can
 be called when the tables change to make sure there are no cycles.

 */
#include "rodsClient.h"
#include "icatHighLevelRoutines.h"
#include "icatMidLevelRoutines.h"
#include "icatLowLevel.h"

#define LIMIT_AUDIT_ACCESS 1  /* undefine this if you want to allow
                                 access to the audit tables by
                                 non-privileged users */

int logSQLGenQuery=0;

void icatGeneralQuerySetup();
int insertWhere(char *condition, int option);

/* use a column size of at least this many characters: */
#define MINIMUM_COL_SIZE 50

extern icatSessionStruct *chlGetRcs();

#define MAX_LINKS_TABLES_OR_COLUMNS 150
#define MAX_TSQL 100

int firstCall=1;

char selectSQL[MAX_SQL_SIZE];
char fromSQL[MAX_SQL_SIZE];
char whereSQL[MAX_SQL_SIZE];
char orderBySQL[MAX_SQL_SIZE];
char groupBySQL[MAX_SQL_SIZE];
int mightNeedGroupBy;
int fromCount;
char accessControlUserName[MAX_NAME_LEN];
char accessControlZone[MAX_NAME_LEN];
int accessControlPriv;
int accessControlControlFlag=0;

struct tlinks {
   int table1;
   int table2;
   char connectingSQL[MAX_TSQL];
} Links [MAX_LINKS_TABLES_OR_COLUMNS];

int nLinks;

struct tTables {
   char tableName[NAME_LEN];
   char tableAlias[MAX_TSQL];
   int cycler;
   int flag;
   char tableAbbrev[2];
} Tables [MAX_LINKS_TABLES_OR_COLUMNS];

int nTables;

struct tColumns {
   int defineValue;
   char columnName[NAME_LEN];
   char tableName[NAME_LEN];
} Columns [MAX_LINKS_TABLES_OR_COLUMNS];

int nColumns;

int nToFind;

char tableAbbrevs;

int debug=0;
int debug2=0;

/*
 Used by fklink (below) to find an existing name and return the
 value.  Once the table is set up, the code can use the integer
 table1 and table2 values to more quickly compare.
 */
int fkFindName(char *tableName) {
   int i;
   for (i=0;i<nTables;i++) {
      if (strcmp(Tables[i].tableName, tableName)==0) {
	 return(i);
      }	 
   }
   rodsLog(LOG_ERROR, "fkFindName table %s unknown", tableName);
   return(0);
}

/*
sFklink - Setup Foreign Key Link.  This is called from the initization
routine with various parameters, to set up a table of links between DB
tables.
 */
int
sFklink(char *table1, char *table2, char *connectingSQL) {
   if (nLinks >= MAX_LINKS_TABLES_OR_COLUMNS) {
      rodsLog(LOG_ERROR, "fklink table full %d", CAT_TOO_MANY_TABLES );
      return( CAT_TOO_MANY_TABLES );
   }
   Links[nLinks].table1 = fkFindName(table1);
   Links[nLinks].table2 = fkFindName(table2);
   strncpy(Links[nLinks].connectingSQL, connectingSQL, MAX_TSQL);
   if (debug>1) printf("link %d is from %d to %d\n", nLinks, 
	  Links[nLinks].table1,
	  Links[nLinks].table2);
   if (debug2) printf("T%2.2d L%2.2d T%2.2d\n", 
	  Links[nLinks].table1,  nLinks, 
	  Links[nLinks].table2);
   nLinks++;
   return(0);
}

/*
sTableInit - initialize the Tables, Links, and Columns tables.
 */
int
sTableInit()
{
      nLinks=0;
      nTables=0;
      nColumns=0;
      memset (Links, 0, sizeof (Links));
      memset (Tables, 0, sizeof (Tables));
      memset (Columns, 0, sizeof (Columns));
      return(0);
}

/*
 sTable setup tables.

 Set up a C table of DB Tables.
 Similar to fklink, this is called with different values to initialize
 a table used within this module.

 The "cycler" flag is 1 if this table could cause cycles.  When a node
 of this type is reached, the subtrees from there are not searched so
 as to avoid loops.

*/
int 
sTable(char *tableName, char *tableAlias, int cycler) {
   if (nTables >= MAX_LINKS_TABLES_OR_COLUMNS) {
      rodsLog(LOG_ERROR, "sTable table full %d", CAT_TOO_MANY_TABLES );
      return( CAT_TOO_MANY_TABLES );
   }
   strncpy(Tables[nTables].tableName, tableName, NAME_LEN);
   strncpy(Tables[nTables].tableAlias, tableAlias, MAX_TSQL);
   Tables[nTables].cycler = cycler;
   if (debug>1) printf("table %d is %s\n", nTables, tableName);
   nTables++;
   return(0);
}

int 
sColumn(int defineVal, char *tableName, char *columnName) {
   if (nColumns >= MAX_LINKS_TABLES_OR_COLUMNS) {
      rodsLog(LOG_ERROR, "sTable table full %d", CAT_TOO_MANY_TABLES );
      return( CAT_TOO_MANY_TABLES );
   }
   strncpy(Columns[nColumns].tableName, tableName, NAME_LEN);
   strncpy(Columns[nColumns].columnName, columnName, NAME_LEN);
   Columns[nColumns].defineValue = defineVal;
   if (debug>1) printf("column %d is %d %s %s\n", 
	  nColumns, defineVal, tableName, columnName);
   nColumns++;
   return(0);
}

/* given a defineValue, return the table and column names;
   called from icatGeneralUpdate functions */
int
sGetColumnInfo(int defineVal, char **tableName, char **columnName) {
   int i;
   for (i=0;i<nColumns;i++) {
      if (Columns[i].defineValue==defineVal) {
	 *tableName=Columns[i].tableName;
	 *columnName=Columns[i].columnName;
	 return(0);
      }
   }
   return(CAT_INVALID_ARGUMENT);
}


/* Determine if a table is present in some sqlText.  The table can be
   a simple table name, or of the form "tableName1 tableName2", where
   1 is being aliased to 2.  If the input table is just one token,
   then even if it matches a table in the tableName1 position, it is
   not a match.
 */
int
tablePresent(char *table, char *sqlText) {
   int tokens, blank;
   char *cp1, *cp2;

   if (debug>1) printf("tablePresent table:%s:\n",table);
   if (debug>1) printf("tablePresent sqlText:%s:\n",sqlText);

   if (strstr(sqlText, table)==NULL) {
      if (debug>1) printf("tablePresent return 0 (simple)\n");
      return(0); /* simple case */
   }

   tokens=0;
   blank=1;
   cp1=table;
   for (;*cp1!='\0' && *cp1!=',';cp1++) {
      if (*cp1==' ') {
	 if (blank==0) tokens++;
	 blank=1;
      }
      else {
	 blank=0;
      }
   }
   if (blank==0) tokens++;

   if (debug>1) printf("tablePresent tokens=%d\n",tokens);
   if (tokens==2) {
      return(1);  /* 2 tokens and did match, is present */
   }

   /* have to check if the token appears in the first or second position */
   tokens=0;
   blank=1;
   cp1=sqlText;
   for(;;) {
      cp2=strstr(cp1, table);
      if (cp2==NULL) {
	 return(0);
      }
      for (;*cp2!='\0' && *cp2!=',';cp2++) {
	 if (*cp2==' ') {
	    if (blank==0) tokens++;
	    blank=1;
	 }
	 else {
	    blank=0;
	 }
      }
      if (blank==0) tokens++;
      if (tokens==1) return(1);
      cp1=cp2;
   }
}

/*
tScan - tree Scan
*/
int
tScan(int table, int link) {
   int thisKeep;
   int subKeep;
   int i;

   if (debug>1) printf("%d tScan\n",table);

   thisKeep = 0;
   if (Tables[table].flag==1) {
      thisKeep=1;
      Tables[table].flag=2;
      nToFind--;
      if (debug>1) printf("nToFind decremented, now=%d\n",nToFind);
      thisKeep=1;
      if (nToFind <= 0) return(thisKeep);
   }
   else {
      if (Tables[table].flag!=0) {  /* not still seeking this one */
	 if (debug>1) printf("%d returning flag=%d\n",table, Tables[table].flag);	 
	 return(0);
      }
   }
   if (Tables[table].cycler==1) {
      if (debug>1) printf("%d returning cycler\n", table);
      return(thisKeep); /* do no more for cyclers */
   }

   Tables[table].flag=3; /* Done with this one, skip it if found again */

   for (i=0;i<nLinks; i++) {
      if (Links[i].table1 == table && link!=i ) {
	 if (debug>1) printf("%d trying link %d forward\n", table, i);
	 subKeep = tScan(Links[i].table2, i);
	 if (debug>1) printf("subKeep %d, this table %d, link %d, table2 %d\n",
			      subKeep, table, i, Links[i].table2);
	 if (subKeep) {
	    thisKeep=1;
	    if (debug>1) printf("%d use link %d\n", table, i);
	    if (strlen(whereSQL)>6) rstrcat(whereSQL, " AND ", MAX_SQL_SIZE);
	    rstrcat(whereSQL, Links[i].connectingSQL, MAX_SQL_SIZE);
	    rstrcat(whereSQL, " ", MAX_SQL_SIZE);
	    if (tablePresent(Tables[Links[i].table2].tableAlias,fromSQL)==0){
	       rstrcat(fromSQL, ", ", MAX_SQL_SIZE);
	       rstrcat(fromSQL, Tables[Links[i].table2].tableAlias, 
		       MAX_SQL_SIZE);
	       rstrcat(fromSQL, " ", MAX_SQL_SIZE);
	    }
	    if (tablePresent(Tables[Links[i].table1].tableAlias, fromSQL)==0){
	       rstrcat(fromSQL, ", ", MAX_SQL_SIZE);
	       rstrcat(fromSQL, Tables[Links[i].table1].tableAlias, 
		       MAX_SQL_SIZE);
	       rstrcat(fromSQL, " ", MAX_SQL_SIZE);
	    }
	    if (debug>1) printf("added (2) to fromSQL: %s\n", fromSQL);
	    if (nToFind <= 0) return(thisKeep);
	 }
      }
   }
   for (i=0;i<nLinks; i++) {
      if (Links[i].table2 == table && link!=i ) {
	 if (debug>1) printf("%d trying link %d backward\n", table, i);
	 subKeep = tScan(Links[i].table1, i);
	 if (debug>1) printf("subKeep %d, this table %d, link %d, table1 %d\n",
			      subKeep, table, i, Links[i].table1);
	 if (subKeep) {
	    thisKeep=1;
	    if (debug>1) printf("%d use link %d\n", table, i);
	    if (strlen(whereSQL)>6) rstrcat(whereSQL, " AND ", MAX_SQL_SIZE);
	    rstrcat(whereSQL, Links[i].connectingSQL, MAX_SQL_SIZE);
	    rstrcat(whereSQL, " ", MAX_SQL_SIZE);
	    if (tablePresent(Tables[Links[i].table2].tableAlias, fromSQL)==0){
	       rstrcat(fromSQL, ", ", MAX_SQL_SIZE);
	       rstrcat(fromSQL, Tables[Links[i].table2].tableAlias, 
		       MAX_SQL_SIZE);
	       rstrcat(fromSQL, " ", MAX_SQL_SIZE);
	    }
            if (tablePresent(Tables[Links[i].table1].tableAlias, fromSQL)==0){
	       rstrcat(fromSQL, ", ", MAX_SQL_SIZE);
	       rstrcat(fromSQL, Tables[Links[i].table1].tableAlias, 
		       MAX_SQL_SIZE);
	       rstrcat(fromSQL, " ", MAX_SQL_SIZE);
	    }
	    if (debug>1) printf("added (3) to fromSQL: %s\n", fromSQL);
	    if (nToFind <= 0) return(thisKeep);
	 }
      }
   }
   if (debug>1) printf("%d returning %d\n",table, thisKeep);
   return(thisKeep);
}

/* prelim test routine, find a subtree between two tables */
int
sTest(int i1, int i2) {
   int i;
   int keepVal;

   if (firstCall) {
      icatGeneralQuerySetup(); /* initialize */
   }
   firstCall=0;

   for (i=0;i<nTables;i++) {
      Tables[i].flag=0;
      if (i==i1 || i==i2) Tables[i].flag=1;
   }
   nToFind=2;
   keepVal = tScan(i1, -1);
   if (keepVal!=1 || nToFind!=0) {
      printf("error failed to link %d to %d\n", i1, i2);
   }
   else {
      printf("SUCCESS linking %d to %d\n", i1, i2);
   }
   return(0);
}

int sTest2(int i1, int i2, int i3) {
   int i;
   int keepVal;

   if (firstCall) {
      icatGeneralQuerySetup(); /* initialize */
   }
   firstCall=0;

   for (i=0;i<nTables;i++) {
      Tables[i].flag=0;
      if (i==i1 || i==i2 || i==i3) Tables[i].flag=1;
   }
   nToFind=3;
   keepVal = tScan(i1, -1);
   if (keepVal!=1 || nToFind!=0) {
      printf("error failed to link %d, %d and %d\n", i1, i2, i3);
   }
   else {
      printf("SUCCESS linking %d, %d, %d\n", i1, i2, i3);
   }
   return(0);
}


/*
tCycleChk - tree Cycle Checker
*/
int
tCycleChk(int table, int link, int thisTreeNum) {
   int thisKeep;
   int subKeep;
   int i;

   if (debug>1) printf("%d tCycleChk\n",table);

   thisKeep = 0;

   if (Tables[table].flag != 0) {
      if (Tables[table].flag == thisTreeNum) {
	 if (debug>1) printf("Found cycle at node %d\n", table);
	 return(1);
      }
   }
   Tables[table].flag = thisTreeNum;

   for (i=0;i<nLinks; i++) {
      if (Links[i].table1 == table && link!=i ) {
	 if (debug>1) printf("%d trying link %d forward\n", table, i);
	 subKeep = tCycleChk(Links[i].table2, i, thisTreeNum);
	 if (subKeep) {
	    thisKeep=1;
	    if (debug>1) printf("%d use link %d tree %d\n", table, i, 
				thisTreeNum);
	    return(thisKeep);
	 }
      }
   }
   for (i=0;i<nLinks; i++) {
      if (Links[i].table2 == table && link!=i ) {
	 if (debug>1) printf("%d trying link %d backward\n", table, i);
	 subKeep = tCycleChk(Links[i].table1, i, thisTreeNum);
	 if (subKeep) {
	    thisKeep=1;
	    if (debug>1) printf("%d use link %d\n", table, i);
	    return(thisKeep);
	 }
      }
   }
   if (debug>1) printf("%d returning %d\n",table, thisKeep);
   return(thisKeep);
}

/* 
 This routine goes thru the tables and links looking for cycles.  If
 there are any cycles, it is a problem as that would mean that there
 are multiple paths between nodes.  So this needs to be run when
 tables or links are changed, but once confirmed that all is OK does
 not need to be run again.
*/
int findCycles(int startTable)
{
   int i, status;
   int treeNum;

   if (firstCall) {
      icatGeneralQuerySetup(); /* initialize */
   }
   firstCall=0;

   for (i=0;i<nTables;i++) {
      Tables[i].flag=0;
   }
   treeNum=0;

   if (startTable != 0) {
      if (startTable > nTables) return(CAT_INVALID_ARGUMENT);
      treeNum++;
      status = tCycleChk(startTable, -1, treeNum);
      if (debug>1) printf("tree %d status %d\n", treeNum, status);
      if (status) return(status);
   }

   for (i=0;i<nTables;i++) {
      if (Tables[i].flag==0) {
	 treeNum++;
	 status = tCycleChk(i, -1, treeNum);
	 if (debug>1) printf("tree %d status %d\n", treeNum, status);
	 if (status) return(status);
      }
   }
   return(0);
}

/*
 Given a column value (from the #define's), select the corresponding
 table.  At the same time, if sel==1, also add another select field to
 selectSQL, fromSQL, whereSQL, and groupBySQL (which is sometimes
 used).  if selectOption is set, then input may be one of the SELECT_*
 modifiers (min, max, etc).
 If the castOption is set, cast the column to a decimal (numeric).
 */
int setTable(int column, int sel, int selectOption, int castOption) {
   int colIx;
   int i;
   int selectOptFlag;

   colIx=0;
   for (i=0;i<nColumns;i++) {
      if (Columns[i].defineValue == column) colIx=i;
   }
   if (colIx == 0) return(CAT_UNKNOWN_TABLE);
      
   for (i=0; i<nTables;i++) {
      if (strcmp(Tables[i].tableName, Columns[colIx].tableName) == 0) {
	 if (Tables[i].flag==0) {
	    nToFind++;
	    Tables[i].tableAbbrev[0]=tableAbbrevs;
	    Tables[i].tableAbbrev[1]='\0';
	    tableAbbrevs++;  
	 }
	 Tables[i].flag=1;
	 if (sel) {
	    if (strlen(selectSQL)>19) rstrcat(selectSQL, ",", MAX_SQL_SIZE);
	    selectOptFlag=0;
	    if (selectOption != 0) {
	       if (selectOption == SELECT_MIN) {
		  rstrcat(selectSQL, "min(", MAX_SQL_SIZE);
		  selectOptFlag=1;
	       }
	       if (selectOption == SELECT_MAX) {
		  rstrcat(selectSQL, "max(", MAX_SQL_SIZE);
		  selectOptFlag=1;
	       }
	       if (selectOption == SELECT_SUM) {
		  rstrcat(selectSQL, "sum(", MAX_SQL_SIZE);
		  selectOptFlag=1;
	       }
	       if (selectOption == SELECT_AVG) {
		  rstrcat(selectSQL, "avg(", MAX_SQL_SIZE);
		  selectOptFlag=1;
	       }
	       if (selectOption == SELECT_COUNT) {
		  rstrcat(selectSQL, "count(", MAX_SQL_SIZE);
		  selectOptFlag=1;
	       }
	    }
	    rstrcat(selectSQL, Tables[i].tableName, MAX_SQL_SIZE);
	    rstrcat(selectSQL, ".", MAX_SQL_SIZE);
	    rstrcat(selectSQL, Columns[colIx].columnName, MAX_SQL_SIZE);
	    rstrcat(selectSQL, " ", MAX_SQL_SIZE);
	    if (selectOptFlag) {
	       rstrcat(selectSQL, ") ", MAX_SQL_SIZE);
	       mightNeedGroupBy=1;
	    }
	    else {
	       if (strlen(groupBySQL)>10) rstrcat(groupBySQL,",",MAX_SQL_SIZE);
	       rstrcat(groupBySQL, Tables[i].tableName, MAX_SQL_SIZE);
	       rstrcat(groupBySQL, ".", MAX_SQL_SIZE);
	       rstrcat(groupBySQL, Columns[colIx].columnName, MAX_SQL_SIZE);
	       rstrcat(groupBySQL, " ", MAX_SQL_SIZE);
	    }

	    if (tablePresent(Tables[i].tableAlias, fromSQL)==0){
	       if (fromCount) {
		  rstrcat(fromSQL, ", ", MAX_SQL_SIZE);
	       }
	       else {
		  rstrcat(fromSQL, " ", MAX_SQL_SIZE);
	       }
	       fromCount++;
	       rstrcat(fromSQL, Tables[i].tableAlias, MAX_SQL_SIZE);
	       rstrcat(fromSQL, " ", MAX_SQL_SIZE);
	    }
	    if (debug>1) printf("added (1) to fromSQL: %s\n", fromSQL);
	 }
	 else {

	    if (strlen(whereSQL)>6) rstrcat(whereSQL, " AND ", MAX_SQL_SIZE);
	    if (castOption==1) {
	       rstrcat(whereSQL, "cast (", MAX_SQL_SIZE);
	    }
	    rstrcat(whereSQL, Tables[i].tableName, MAX_SQL_SIZE);
	    rstrcat(whereSQL, ".", MAX_SQL_SIZE);
	    rstrcat(whereSQL, Columns[colIx].columnName, MAX_SQL_SIZE);
	    if (castOption==1) {
               /* In PostgreSQL and Oracle 'decimal' is the same as
                  'numeric', MySQL allows 'decimal' but not 'numeric',
                  so we cast it to decimal for any of them. */
	       rstrcat(whereSQL, " as decimal)", MAX_SQL_SIZE);
	    }
	 }
	 if (debug>1) printf("table index=%d, nToFind=%d\n",i, nToFind);
	 return(i);
      }
   }
   return(-1);
}

/*
 When there are multiple AVU conditions, need to adjust the SQL.
 */
void 
handleMultiDataAVUConditions(int nConditions) {
   int i;
   char *firstItem, *nextItem;
   char nextStr[20];

   /* In the whereSQL, change r_data_meta_main.meta_attr_name to
      r_data_meta_mnNN.meta_attr_name, where NN is index.  First one
      is OK, subsequent ones need a new name for each. */
   firstItem = strstr(whereSQL, "r_data_meta_main.meta_attr_name");
   if (firstItem != NULL) {
      *firstItem = 'x'; /* temporarily change 1st string */
   }
   for (i=2;i<=nConditions;i++) {
      nextItem = strstr(whereSQL, "r_data_meta_main.meta_attr_name");
      if (nextItem != NULL) {
	 snprintf(nextStr, 20, "n%2.2d", i);
	 *(nextItem+13)=nextStr[0];  /* replace "ain" in main */
	 *(nextItem+14)=nextStr[1];  /* with nNN */
	 *(nextItem+15)=nextStr[2];
      }
   }
   if (firstItem != NULL) {
      *firstItem = 'r'; /* put it back */
   }

   /* Do similar for r_data_meta_main.meta_attr_value.  */
   firstItem = strstr(whereSQL, "r_data_meta_main.meta_attr_value");
   if (firstItem != NULL) {
      *firstItem = 'x'; /* temporarily change 1st string */
   }
   for (i=2;i<=nConditions;i++) {
      nextItem = strstr(whereSQL, "r_data_meta_main.meta_attr_value");
      if (nextItem != NULL) {
	 snprintf(nextStr, 20, "n%2.2d", i);
	 *(nextItem+13)=nextStr[0];  /* replace "ain" in main */
	 *(nextItem+14)=nextStr[1];  /* with nNN */
	 *(nextItem+15)=nextStr[2];
      }
   }
   if (firstItem != NULL) {
      *firstItem = 'r'; /* put it back */
   }

 
   /* In the fromSQL, add items for r_data_metamapNN and
      r_data_meta_mnNN */
   for (i=2;i<=nConditions;i++) {
      char newStr[100];
      snprintf(newStr, 100,
       ", r_objt_metamap r_data_metamap%d, r_meta_main r_data_meta_mn%2.2d ", 
	       i, i);
      rstrcat(fromSQL, newStr, MAX_SQL_SIZE);
   }

   /* In the whereSQL, add items for 
      r_data_metamapNN.meta_id = r_data_meta_maNN.meta_id  and
      r_data_main.data_id = r_data_metamap2.object_id
   */
   for (i=2;i<=nConditions;i++) {
      char newStr[100];
      snprintf(newStr, 100,
       " AND r_data_metamap%d.meta_id = r_data_meta_mn%2.2d.meta_id", i, i);
      rstrcat(whereSQL, newStr, MAX_SQL_SIZE);
      snprintf(newStr, 100,
	       " AND r_data_main.data_id = r_data_metamap%d.object_id ", i);
      rstrcat(whereSQL, newStr, MAX_SQL_SIZE);
   }

}

/* When there's a compound condition, need to put () around it, use the 
   tablename.column for each part, and put OR or AND between.
   Uses and updates whereSQL in addition to the arguments.
*/
int
handleCompoundCondition(char *condition, int prevWhereLen)
{
   char tabAndColumn[MAX_SQL_SIZE];
   char condPart1[MAX_NAME_LEN*2];
   static char condPart2[MAX_NAME_LEN*2];
   static char conditionsForBind[MAX_NAME_LEN*2];
   static int conditionsForBindIx=0;
   int type;
   char *cptr;
   int status;
   int i;
   int first=1;
   int keepGoing=1;


   rstrcpy(condPart1, condition, MAX_NAME_LEN*2);

   while (keepGoing) {
      type=0;
      cptr = strstr(condPart1, "||");
      if (cptr != NULL) {
	 type=1;
      }
      else {
	 cptr = strstr(condPart1, "&&");
	 type=2;
      }
      if (type) {
	 *cptr='\0';
	 cptr+=2; /* past the && or || */
	 rstrcpy(condPart2, cptr, MAX_NAME_LEN*2);
      }

      if (first) {
	 /* If there's an AND that was appended, need to include it */
	 i = prevWhereLen;
	 if (whereSQL[i]==' ') i++;
	 if (whereSQL[i]==' ') i++;
	 if (whereSQL[i]=='A') {
	    i++;
	    if (whereSQL[i]=='N') {
	       i++;
	       if (whereSQL[i]=='D') {
		  i++;
		  prevWhereLen=i;
	       }
	    }
	 }

	 rstrcpy(tabAndColumn, (char *)&whereSQL[prevWhereLen], MAX_SQL_SIZE);
	 whereSQL[prevWhereLen]='\0'; /* reset whereSQL to previous spot */
	 rstrcat(whereSQL, " ( ", MAX_SQL_SIZE);
      }
      rstrcat(whereSQL, tabAndColumn, MAX_SQL_SIZE);
      rstrcpy((char*)&conditionsForBind[conditionsForBindIx], condPart1, 
	      (MAX_SQL_SIZE*2)-conditionsForBindIx);
      status = insertWhere((char*)&conditionsForBind[conditionsForBindIx], 0);
      if (status) return(status);
      conditionsForBindIx+=strlen(condPart1)+1;

      if (type==1) {
	 rstrcat(whereSQL, " OR ", MAX_SQL_SIZE);
      }
      else {
	 rstrcat(whereSQL, " AND ", MAX_SQL_SIZE);
      }

      if (strstr(condPart2, "||") == NULL &&
	  strstr(condPart2, "&&") == NULL) {
	 rstrcat(whereSQL, tabAndColumn, MAX_SQL_SIZE);
	 status = insertWhere(condPart2, 0);
	 if (status) return(status);
	 keepGoing=0;
      }
      else {
	 rstrcpy(condPart1, condPart2, MAX_NAME_LEN*2);
      }
      first=0;
   }

   rstrcat(whereSQL, " ) ", MAX_SQL_SIZE);
   return(0);
}

/* Set the orderBySQL clause if needed */
void 
setOrderBy(genQueryInp_t genQueryInp, int column) {
   int i, j;
   int selectOpt, isAggregated;
   for (i=0;i<genQueryInp.selectInp.len;i++) { 
      if (genQueryInp.selectInp.inx[i] == column) {
	 selectOpt = genQueryInp.selectInp.value[i]&0xf;
	 isAggregated=0;
	 if (selectOpt == SELECT_MIN) isAggregated=1;
	 if (selectOpt == SELECT_MAX) isAggregated=1;
	 if (selectOpt == SELECT_SUM) isAggregated=1;
	 if (selectOpt == SELECT_AVG) isAggregated=1;
	 if (selectOpt == SELECT_COUNT) isAggregated=1;
	 if (isAggregated==0) {
	    for (j=0;j<nColumns;j++) {
	       if (Columns[j].defineValue == column) {
		  if (strlen(orderBySQL)>10) {
		     rstrcat(orderBySQL, ", ", MAX_SQL_SIZE);
		  }
		  rstrcat(orderBySQL, Columns[j].tableName, MAX_SQL_SIZE);
		  rstrcat(orderBySQL, ".", MAX_SQL_SIZE);
		  rstrcat(orderBySQL, Columns[j].columnName, MAX_SQL_SIZE);
		  break;
	       }
	    }
	 }
      }
   }
}

/* 
  Set the user-requested order-by clauses (if any)
 */
void
setOrderByUser(genQueryInp_t genQueryInp) {
   int i, j, done;
   for (i=0;i<genQueryInp.selectInp.len;i++) { 
      if ( (genQueryInp.selectInp.value[i] & ORDER_BY) ||
	   (genQueryInp.selectInp.value[i] & ORDER_BY_DESC) )  {
	 done=0;
	 for (j=0;j<nColumns && done==0;j++) {
	    if (Columns[j].defineValue == genQueryInp.selectInp.inx[i]) {
	       if (strlen(orderBySQL)>10) {
		  rstrcat(orderBySQL, ", ", MAX_SQL_SIZE);
	       }
	       rstrcat(orderBySQL, Columns[j].tableName, MAX_SQL_SIZE);
	       rstrcat(orderBySQL, ".", MAX_SQL_SIZE);
	       rstrcat(orderBySQL, Columns[j].columnName, MAX_SQL_SIZE);
	       if (genQueryInp.selectInp.value[i] & ORDER_BY_DESC) {
		  rstrcat(orderBySQL, " DESC ", MAX_SQL_SIZE);
	       }
	       done=1;
	    }
	 }
      }
   }
}

int
setBlank(char *string, int count) {
   int i;
   char *cp;
   for (cp=string,i=0;i<count;i++) {
      *cp++=' ';
   }
   return(0);
}

/*
Verify that a condition is a valid SQL condition
 */
int
checkCondition(char *condition) {
   char tmpStr[25];
   char *cp;

   rstrcpy(tmpStr, condition, 20);
   for (cp=tmpStr;*cp!='\0';cp++) {
      if (*cp=='<') *cp=' ';
      if (*cp=='>') *cp=' ';
      if (*cp=='=') *cp=' ';
   }
   cp = strstr(tmpStr,"begin_of");
   if (cp != NULL) setBlank(cp, 8);
   cp = strstr(tmpStr,"not");
   if (cp != NULL) setBlank(cp, 3);
   cp = strstr(tmpStr,"NOT");
   if (cp != NULL) setBlank(cp, 3);
   cp = strstr(tmpStr,"between");
   if (cp != NULL) setBlank(cp, 7);
   cp = strstr(tmpStr,"BETWEEN");
   if (cp != NULL) setBlank(cp, 7);
   cp = strstr(tmpStr,"like");
   if (cp != NULL) setBlank(cp, 4);
   cp = strstr(tmpStr,"LIKE");
   if (cp != NULL) setBlank(cp, 4);
   cp = strstr(tmpStr,"in");
   if (cp != NULL) setBlank(cp, 2);
   cp = strstr(tmpStr,"IN");
   if (cp != NULL) setBlank(cp, 2);

   for (cp=tmpStr;*cp!='\0';cp++) {
      if (*cp != ' ') return(CAT_INVALID_ARGUMENT);
   }
   return(0);
}

/*
insert a new where clause using bind-variables
 */
int
insertWhere(char *condition, int option) {
   static int bindIx=0;
   static char bindVars[MAX_SQL_SIZE+100];
   char *cp1, *cpFirstQuote, *cpSecondQuote;
   char *cp;
   int i;
   char *thisBindVar;
   char tmpStr[20];
   char myCondition[20];
   char tmpStr2[MAX_SQL_SIZE];

   /*  Old way 
   rstrcat(whereSQL, " ", MAX_SQL_SIZE);
   rstrcat(whereSQL, condition, MAX_SQL_SIZE);
   rstrcat(whereSQL, " ", MAX_SQL_SIZE);
   */
   /* Now, with Bind variables */
   if (option==1) { /* reinitialize */
      bindIx=0;
      return(0);
   }
   cpFirstQuote=0;
   cpSecondQuote=0;
   for (cp1=condition;*cp1!='\0';cp1++) {
      if (*cp1=='\'') {
	 if (cpFirstQuote==0) {
	    cpFirstQuote=cp1;
	 }
	 else {
	    cpSecondQuote=cp1; /* If embedded 's, skip them; it's OK*/
	 }
      }
   }
   bindIx++;
   thisBindVar=(char*)&bindVars[bindIx];
   if (cpFirstQuote==0 || cpSecondQuote==0) {
      return(CAT_INVALID_ARGUMENT);
   }
   if ((cpSecondQuote-cpFirstQuote)+bindIx > MAX_SQL_SIZE+90) 
      return(CAT_INVALID_ARGUMENT);

   for (cp1=cpFirstQuote+1;cp1<cpSecondQuote;cp1++) {
      bindVars[bindIx++]=*cp1;
   }
   bindVars[bindIx++]='\0';
   cllBindVars[cllBindVarCount++]=thisBindVar;

 /* basic legality check on the condition */
   if ((cpFirstQuote-condition) > 10) return(CAT_INVALID_ARGUMENT);

   tmpStr[0]=' ';
   i=1;
   for (cp1=condition;;) {
      tmpStr[i++]= *cp1++;
      if (cp1==cpFirstQuote) break;
   }
   tmpStr[i]='\0';
   rstrcpy(myCondition, tmpStr, 20);

   cp = strstr(myCondition,"begin_of");
   if (cp != NULL) {
      cp1=whereSQL+strlen(whereSQL)-1;
      while (*cp1!=' ') cp1--;
      cp1++;
      rstrcpy(tmpStr2, cp1, MAX_SQL_SIZE); /*use table/column name just added*/
#if ORA_ICAT
      rstrcat(whereSQL, "=substr(?,1,length(", MAX_SQL_SIZE);
      rstrcat(whereSQL, tmpStr2, MAX_SQL_SIZE);
      rstrcat(whereSQL, "))", MAX_SQL_SIZE);
      rstrcat(whereSQL, " AND length(", MAX_SQL_SIZE);
#else
      rstrcat(whereSQL, "=substr(?,1,char_length(", MAX_SQL_SIZE);
      rstrcat(whereSQL, tmpStr2, MAX_SQL_SIZE);
      rstrcat(whereSQL, "))", MAX_SQL_SIZE);
      rstrcat(whereSQL, " AND char_length(", MAX_SQL_SIZE);
#endif
      rstrcat(whereSQL, tmpStr2, MAX_SQL_SIZE);
      rstrcat(whereSQL, ")>0", MAX_SQL_SIZE);
   }
   else {
      tmpStr[i++]='?';
      tmpStr[i++]=' ';
      tmpStr[i++]='\0';
      rstrcat(whereSQL, tmpStr, MAX_SQL_SIZE);
   }

   return(checkCondition(myCondition));
}

/* 
 Only used if requested by msiAclPolicy (acAclPolicy rule) (which
 normally isn't) or if the user is anonymous.  This restricts
 r_data_main anc r_coll_main info to only users with access.
 If client user is the local admin, do not restrict.
 */
int
genqAppendAccessCheck() {
   int doCheck=0;
   int ACDebug=0;

   if (ACDebug) printf("genqAC 1\n");

   if (accessControlPriv==LOCAL_PRIV_USER_AUTH) return(0); 

   if (ACDebug) printf("genqAC 2 accessControlControlFlag=%d\n",
		       accessControlControlFlag);

   if (accessControlControlFlag > 1) {
      doCheck=1;
   }

   if (ACDebug) printf("genqAC 3\n");

   if (doCheck==0) {
      if (strncmp(accessControlUserName,ANONYMOUS_USER, MAX_NAME_LEN)==0) {
	 doCheck=1;
      }
   }

   if (doCheck==0) return(0);

   if (ACDebug)  printf("genqAC 4\n");

   /* if an item in r_data_main is being accessed, add a
      (complicated) addition to the where clause to check access */
   if (strstr(selectSQL, "r_data_main") != NULL) {
      if (strlen(whereSQL)>6) rstrcat(whereSQL, " AND ", MAX_SQL_SIZE);
      cllBindVars[cllBindVarCount++]=accessControlUserName;
      cllBindVars[cllBindVarCount++]=accessControlZone;
      rstrcat(whereSQL, "r_data_main.data_id in (select object_id from r_objt_access OA, r_user_group UG, r_user_main UM, r_tokn_main TM where UM.user_name=? and UM.zone_name=? and UM.user_type_name!='rodsgroup' and UM.user_id = UG.user_id and UG.group_user_id = OA.user_id and OA.object_id = r_data_main.data_id and OA.access_type_id >= TM.token_id and  TM.token_namespace ='access_type' and TM.token_name = 'read object')", MAX_SQL_SIZE);
   }

   /* if an item in r_coll_main is being accessed, add a
      (complicated) addition to the where clause to check access */
   if (strstr(selectSQL, "r_coll_main") != NULL) {
      if (strlen(whereSQL)>6) rstrcat(whereSQL, " AND ", MAX_SQL_SIZE);
      cllBindVars[cllBindVarCount++]=accessControlUserName;
      cllBindVars[cllBindVarCount++]=accessControlZone;
      rstrcat(whereSQL, "r_coll_main.coll_id in (select object_id from r_objt_access OA, r_user_group UG, r_user_main UM, r_tokn_main TM where UM.user_name=? and UM.zone_name=? and UM.user_type_name!='rodsgroup' and UM.user_id = UG.user_id and OA.object_id = r_coll_main.coll_id and UG.group_user_id = OA.user_id and OA.access_type_id >= TM.token_id and  TM.token_namespace ='access_type' and TM.token_name = 'read object')", MAX_SQL_SIZE);
   }
   return(0);
}


/* 
Called by chlGenQuery to generate the SQL.
*/
int
generateSQL(genQueryInp_t genQueryInp, char *resultingSQL, 
	    char *resultingCountSQL) {
   int i, table;
   int keepVal;
   char *condition;
   int status;
   int useGroupBy;
   int N_col_meta_data_attr_name=0;

   char combinedSQL[MAX_SQL_SIZE];
#if ORA_ICAT
   char countSQL[MAX_SQL_SIZE];
#else
   static char offsetStr[20];
#endif

   if (firstCall) {
      icatGeneralQuerySetup(); /* initialize */
   }
   firstCall=0;

   nToFind = 0;
   for (i=0;i<nTables;i++) {
      Tables[i].flag=0;
   }

   insertWhere("",1); /* initialize */
   rstrcpy(selectSQL, "select distinct ", MAX_SQL_SIZE);
   rstrcpy(fromSQL, "from ", MAX_SQL_SIZE);
   fromCount=0;
   rstrcpy(whereSQL, "where ", MAX_SQL_SIZE);
   rstrcpy(groupBySQL, "group by ", MAX_SQL_SIZE);
   mightNeedGroupBy=0;

   tableAbbrevs='a'; /* reset */

   for (i=0;i<genQueryInp.selectInp.len;i++) {
      table = setTable(genQueryInp.selectInp.inx[i], 1, 
		       genQueryInp.selectInp.value[i]&0xf, 0);
      if (table < 0) {
	 rodsLog(LOG_ERROR,"Table for column %d not found\n",
		genQueryInp.selectInp.inx[i]);
	 return(CAT_UNKNOWN_TABLE);
      }
#ifdef LIMIT_AUDIT_ACCESS
      if (genQueryInp.selectInp.inx[i] >= COL_AUDIT_RANGE_START &&
	  genQueryInp.selectInp.inx[i] <= COL_AUDIT_RANGE_END) {
	 if (accessControlPriv != LOCAL_PRIV_USER_AUTH) {
	    return(CAT_NO_ACCESS_PERMISSION);
	 }
      }
#endif
   }

   for (i=0;i<genQueryInp.sqlCondInp.len;i++) {
      int prevWhereLen;
      int castOption;
      char *cptr;

      prevWhereLen = strlen(whereSQL);
      if (genQueryInp.sqlCondInp.inx[i]==COL_META_DATA_ATTR_NAME) {
	 N_col_meta_data_attr_name++;
      }
/*
  Using an input condition, determine if the associated column is being
  requested to be cast as an int.  That is, if the input is n< n> or n=.
 */
      castOption=0;
      cptr = genQueryInp.sqlCondInp.value[i];
      while (*cptr==' ') cptr++;
      if ( (*cptr=='n' && *(cptr+1)=='<') ||
           (*cptr=='n' && *(cptr+1)=='>') ||
           (*cptr=='n' && *(cptr+1)=='=') ) {
	 castOption=1;
	 *cptr=' ';   /* clear the 'n' that was just checked so what
                         remains is proper SQL */
      }
      table = setTable(genQueryInp.sqlCondInp.inx[i], 0, 0,
		       castOption);
      if (table < 0) {
	 rodsLog(LOG_ERROR,"Table for column %d not found\n",
		genQueryInp.sqlCondInp.inx[i]);
	 return(CAT_UNKNOWN_TABLE);
      }
      condition = genQueryInp.sqlCondInp.value[i];
      if (strstr(condition, "||") != NULL ||
	  strstr(condition, "&&") != NULL) {
	 status = handleCompoundCondition(condition, prevWhereLen);
	 if (status) return(status);
      }
      else {
	 status = insertWhere(condition, 0);
	 if (status) return(status);
      }
#ifdef LIMIT_AUDIT_ACCESS
      if (genQueryInp.sqlCondInp.inx[i] >= COL_AUDIT_RANGE_START &&
	  genQueryInp.sqlCondInp.inx[i] <= COL_AUDIT_RANGE_END) {
	 if (accessControlPriv != LOCAL_PRIV_USER_AUTH) {
	    return(CAT_NO_ACCESS_PERMISSION);
	 }
      }
#endif
   }

   keepVal = tScan(table, -1);
   if (keepVal!=1 || nToFind!=0) {
      rodsLog(LOG_ERROR,"error failed to link tables\n");
      return(CAT_FAILED_TO_LINK_TABLES);
   }
   else {
      if (debug>1) printf("SUCCESS linking tables\n");
   }

   if (N_col_meta_data_attr_name > 1) {
      /* Need to do some special changes and additions for multi AVU query */
      handleMultiDataAVUConditions(N_col_meta_data_attr_name);
   }

   if (debug) printf("selectSQL: %s\n",selectSQL);
   if (debug) printf("fromSQL: %s\n",fromSQL);
   if (debug) printf("whereSQL: %s\n",whereSQL);
   useGroupBy=0;
   if (mightNeedGroupBy) {
      if (strlen(groupBySQL)>10) useGroupBy=1;
   }
   if (debug && useGroupBy) printf("groupBySQL: %s\n",groupBySQL);

   combinedSQL[0]='\0';
   rstrcat(combinedSQL, selectSQL, MAX_SQL_SIZE);
   rstrcat(combinedSQL, " " , MAX_SQL_SIZE);
   rstrcat(combinedSQL, fromSQL, MAX_SQL_SIZE);

   genqAppendAccessCheck();

   if (strlen(whereSQL)>6) {
      rstrcat(combinedSQL, " " , MAX_SQL_SIZE);
      rstrcat(combinedSQL, whereSQL, MAX_SQL_SIZE);
   }
   if (useGroupBy) {
      rstrcat(combinedSQL, " " , MAX_SQL_SIZE);
      rstrcat(combinedSQL, groupBySQL, MAX_SQL_SIZE);
   }
   rstrcpy(orderBySQL, " order by ", MAX_SQL_SIZE);
   setOrderByUser(genQueryInp);
   setOrderBy(genQueryInp, COL_COLL_NAME);
   setOrderBy(genQueryInp, COL_DATA_NAME);
   setOrderBy(genQueryInp, COL_DATA_REPL_NUM);
   if (strlen(orderBySQL)>10) {
      rstrcat(combinedSQL, orderBySQL, MAX_SQL_SIZE);
   }

   if (genQueryInp.rowOffset > 0) {
#if ORA_ICAT
      /* For Oracle, it may be possible to do this by surrounding the
         select with another select and using rownum or row_number(),
         but there are a number of subtle problems/special cases to
         deal with.  So instead, we handle this elsewhere by getting
         and disgarding rows. */
#elif MY_ICAT
   /* MySQL/ODBC handles it nicely via just adding limit/offset */
      snprintf (offsetStr, 20, "%d", genQueryInp.rowOffset);
      rstrcat(combinedSQL, " limit ", MAX_SQL_SIZE);
      rstrcat(combinedSQL, offsetStr, MAX_SQL_SIZE);
      rstrcat(combinedSQL, ",18446744073709551615", MAX_SQL_SIZE);
#else
   /* Postgres/ODBC handles it nicely via just adding offset */
      snprintf (offsetStr, 20, "%d", genQueryInp.rowOffset);
      cllBindVars[cllBindVarCount++]=offsetStr;
      rstrcat(combinedSQL, " offset ?", MAX_SQL_SIZE);
#endif
   }

   if (debug) printf("combinedSQL=:%s:\n",combinedSQL);
   strncpy(resultingSQL, combinedSQL, MAX_SQL_SIZE);

#if ORA_ICAT
   countSQL[0]='\0';
   rstrcat(countSQL, "select distinct count(*) ", MAX_SQL_SIZE);
   rstrcat(countSQL, fromSQL, MAX_SQL_SIZE);

   if (strlen(whereSQL)>6) {
      rstrcat(countSQL, " " , MAX_SQL_SIZE);
      rstrcat(countSQL, whereSQL, MAX_SQL_SIZE);
   }

   if (debug) printf("countSQL=:%s:\n",countSQL);
   strncpy(resultingCountSQL, countSQL, MAX_SQL_SIZE);
#endif
   return(0);
}

/*
 Perform a check based on the condInput parameters;
 Verify that the user has access to the dataObj at the requested level.

 If continueFlag is non-zero this is a continuation (more rows), so if
 the dataId is the same, can skip the check to the db.
 */
int
checkCondInputAccess(genQueryInp_t genQueryInp, int statementNum, 
		     icatSessionStruct *icss, int continueFlag) {
   int i, nCols;
   int userIx=-1, zoneIx=-1, accessIx=-1, dataIx=-1, collIx=-1;
   int status;
   char *zoneName;
   static char prevDataId[LONG_NAME_LEN];
   static char prevUser[LONG_NAME_LEN];
   static char prevAccess[LONG_NAME_LEN];
   static int prevStatus;

   for (i=0;i<genQueryInp.condInput.len;i++) {
      if (strcmp(genQueryInp.condInput.keyWord[i],
		USER_NAME_CLIENT_KW)==0)  userIx=i;
      if (strcmp(genQueryInp.condInput.keyWord[i],
		RODS_ZONE_CLIENT_KW)==0)  zoneIx=i;
      if (strcmp(genQueryInp.condInput.keyWord[i],
		ACCESS_PERMISSION_KW)==0)  accessIx=i;

   }
   if (genQueryInp.condInput.len==1 && 
       strcmp(genQueryInp.condInput.keyWord[0], ZONE_KW)==0) {
       return(0);
   }

   if (userIx<0 || zoneIx<0 || accessIx<0) return(CAT_INVALID_ARGUMENT);

   /* Try to find the dataId and/or collID in the output */
   nCols = icss->stmtPtr[statementNum]->numOfCols;
   for (i=0;i<nCols;i++) {
      if (strcmp(icss->stmtPtr[statementNum]->resultColName[i], "data_id")==0) 
	 dataIx = i;
      /* With Oracle the column names are in upper case: */
      if (strcmp(icss->stmtPtr[statementNum]->resultColName[i], "DATA_ID")==0) 
	 dataIx = i;
      if (strcmp(icss->stmtPtr[statementNum]->resultColName[i], "coll_id")==0) 
	 collIx = i;
      /* With Oracle the column names are in upper case: */
      if (strcmp(icss->stmtPtr[statementNum]->resultColName[i], "COLL_ID")==0) 
	 collIx = i;
   }
   if (dataIx<0 && collIx<0) return(CAT_INVALID_ARGUMENT);

   if (dataIx>=0) {
      if (continueFlag==0) {
         if (strcmp(prevDataId, 
                icss->stmtPtr[statementNum]->resultValue[dataIx])==0) {
	    if (strcmp(prevUser, genQueryInp.condInput.value[userIx])==0) {
	       if (strcmp(prevAccess, 
			  genQueryInp.condInput.value[accessIx])==0) {
	       	    return(prevStatus);
	       }
	    }
         }
      }

      strncpy(prevDataId, icss->stmtPtr[statementNum]->resultValue[dataIx],
	      LONG_NAME_LEN);
      strncpy(prevUser, genQueryInp.condInput.value[userIx], 
              LONG_NAME_LEN);
      strncpy(prevAccess, genQueryInp.condInput.value[accessIx], 
              LONG_NAME_LEN);
      prevStatus=0;

      if (strlen(genQueryInp.condInput.value[zoneIx])==0) {
          zoneName = chlGetLocalZone();
      }
      else {
          zoneName = genQueryInp.condInput.value[zoneIx];
      }
      status = cmlCheckDataObjId(
			      icss->stmtPtr[statementNum]->resultValue[dataIx],
			      genQueryInp.condInput.value[userIx],
			      zoneName,
			      genQueryInp.condInput.value[accessIx], icss);
      prevStatus=status;
      return(status);
   }

   if (collIx>=0) {
      if (strlen(genQueryInp.condInput.value[zoneIx])==0) {
          zoneName = chlGetLocalZone();
      }
      else {
          zoneName = genQueryInp.condInput.value[zoneIx];
      }
      status = cmlCheckDirId(
			      icss->stmtPtr[statementNum]->resultValue[collIx],
			      genQueryInp.condInput.value[userIx],
			      zoneName,
  			      genQueryInp.condInput.value[accessIx], icss);
   }
   return(status);
}

/* Save some pre-provided parameters if msiAclPolicy is STRICT.
   Called with user == NULL to set the controlFlag, else with the
   user info.
 */
int 
chlGenQueryAccessControlSetup(char *user, char *zone, int priv, 
                              int controlFlag) {
    if (user != NULL ) {
        rstrcpy(accessControlUserName, user, MAX_NAME_LEN);
	rstrcpy(accessControlZone, zone, MAX_NAME_LEN);
	accessControlPriv=priv;
    }
    if (controlFlag > 0 ) {
       /*
	 If the caller is making this STRICT, then allow the change as
         this will be an initial acAclPolicy call which is setup in
         core.irb.  But don't let users override this admin setting
         via their own calls to the msiAclPolicy; once it is STRICT,
         it stays strict.
       */
       accessControlControlFlag=controlFlag;
    }
    return(0);
}

/* General Query */
int
chlGenQuery(genQueryInp_t genQueryInp, genQueryOut_t *result) {
   int i, j, k;
   int needToGetNextRow;

   char combinedSQL[MAX_SQL_SIZE];
   char countSQL[MAX_SQL_SIZE]; /* For Oracle, sql to get the count */

   int status, statementNum;
   int numOfCols;
   int attriTextLen;
   int totalLen;
   int maxColSize;
   int currentMaxColSize;
   char *tResult, *tResult2;
   static int recursiveCall=0;

   if (logSQLGenQuery) rodsLog(LOG_SQL, "chlGenQuery");

   icatSessionStruct *icss;

   result->attriCnt=0;
   result->rowCnt=0;
   result->totalRowCount = 0;

   currentMaxColSize=0;

   icss = chlGetRcs();
   if (icss==NULL) return(CAT_NOT_OPEN);
   if (debug) printf("icss=%d\n",(int)icss);

   if (genQueryInp.continueInx == 0) {
      status = generateSQL(genQueryInp, combinedSQL, countSQL);
      if (status != 0) return(status);
      if (logSQLGenQuery) {
	 if (genQueryInp.rowOffset==0) {
	    rodsLog(LOG_SQL, "chlGenQuery SQL 1");
	 }
	 else {
	    rodsLog(LOG_SQL, "chlGenQuery SQL 2");
	 }
#if 0
	 rodsLog(LOG_SQL, "combinedSQL: %s", combinedSQL);
#endif
      }

      if (genQueryInp.options & RETURN_TOTAL_ROW_COUNT) {
         /* For Oracle, done just below, for Postgres a little later */
	 if (logSQLGenQuery) rodsLog(LOG_SQL, "chlGenQuery SQL 3");
      }

#if ORA_ICAT
      if (genQueryInp.options & RETURN_TOTAL_ROW_COUNT) {
         int cllBindVarCountSave;
         rodsLong_t iVal;
         cllBindVarCountSave = cllBindVarCount;
         status = cmlGetIntegerValueFromSqlV3(countSQL, &iVal, 
                                            icss);
         if (status < 0) {
	    if (status != CAT_NO_ROWS_FOUND) {
	       rodsLog(LOG_NOTICE,
		 "chlGenQuery cmlGetFirstRowFromSql failure %d",
		 status);
  	    }
	    return(status);
         }
	 if (iVal >= 0) result->totalRowCount = iVal;
         cllBindVarCount = cllBindVarCountSave;
      }
#endif

      status = cmlGetFirstRowFromSql(combinedSQL, &statementNum, 
                                     genQueryInp.rowOffset, icss);
      if (status < 0) {
	 if (status != CAT_NO_ROWS_FOUND) {
	    rodsLog(LOG_NOTICE,
		 "chlGenQuery cmlGetFirstRowFromSql failure %d",
		 status);
	 }
#if ORA_ICAT
#else
         else {
            int saveStatus;
            int newStatus;
            if (genQueryInp.options & RETURN_TOTAL_ROW_COUNT  &&
	        genQueryInp.rowOffset > 0 ) {
/* For Postgres in this  case, need to query again to determine total rows */
               saveStatus = status;
               recursiveCall=1;
               genQueryInp.rowOffset = 0;
               newStatus = chlGenQuery(genQueryInp, result);
               return(saveStatus);
            }
         }
#endif
	 return(status);
      }

#if ORA_ICAT
      recursiveCall=0; /* avoid warning */
#else
      if (genQueryInp.options & RETURN_TOTAL_ROW_COUNT) {
	 i = cllGetRowCount(icss, statementNum);
	 if (i >= 0) result->totalRowCount = i + genQueryInp.rowOffset;
         if (recursiveCall==1) {
             recursiveCall=0;
             return(status);
         }
      }
#endif

      if (genQueryInp.condInput.len > 0) {
	 status = checkCondInputAccess(genQueryInp, statementNum, icss, 0);
	 if (status != 0) return(status);
      }
      result->continueInx = statementNum+1;
      if (debug) printf("statement number =%d\n", statementNum);
      needToGetNextRow = 0;
   }
   else {
      statementNum = genQueryInp.continueInx-1;
      needToGetNextRow = 1;
      if (genQueryInp.maxRows<=0) {  /* caller is closing out the query */
	 status = cmlFreeStatement(statementNum, icss);
	 return(status);
      }
   }
   for (i=0;i<genQueryInp.maxRows;i++) {
      if (needToGetNextRow) {
	 status = cmlGetNextRowFromStatement(statementNum, icss);
	 if (status == CAT_NO_ROWS_FOUND) {
            int status2;
            status2 = cmlFreeStatement(statementNum, icss);
	    result->continueInx=0;
	    if (result->rowCnt==0) return(status); /* NO ROWS; in this 
                       case a continuation call is finding no more rows */
	    return(0);
	 }
	 if (status < 0) return(status);
	 if (genQueryInp.condInput.len > 0) {
	    status = checkCondInputAccess(genQueryInp, statementNum, icss, 1);
	    if (status != 0) return(status);
	 }
      }
      needToGetNextRow = 1;

      result->rowCnt++;
      if (debug) printf("result->rowCnt=%d\n", result->rowCnt);
      numOfCols = icss->stmtPtr[statementNum]->numOfCols;
      if (debug) printf("numOfCols=%d\n",numOfCols);
      result->attriCnt=numOfCols;
      result->continueInx = statementNum+1;

      maxColSize=0;

      for (k=0;k<numOfCols;k++) {
	 j = strlen(icss->stmtPtr[statementNum]->resultValue[k]);
	 if (maxColSize <= j) maxColSize=j;
      }
      maxColSize++; /* for the null termination */
      if (maxColSize < MINIMUM_COL_SIZE) {
	 maxColSize=MINIMUM_COL_SIZE;  /* make it a reasonable size */
      }
      if (debug) printf("maxColSize=%d\n",maxColSize);

      if (i==0) {  /* first time thru, allocate and initialize */
	  attriTextLen= numOfCols * maxColSize;
	 if (debug) printf("attriTextLen=%d\n",attriTextLen);
	 totalLen = attriTextLen * genQueryInp.maxRows;
	 for (j=0;j<numOfCols;j++) {
	    tResult = malloc(totalLen);
	    if (tResult==NULL) return(SYS_MALLOC_ERR);
	    memset(tResult, 0, totalLen);
	    result->sqlResult[j].attriInx = genQueryInp.selectInp.inx[j];
	    result->sqlResult[j].len = maxColSize;
	    result->sqlResult[j].value = tResult;
	 }
	 currentMaxColSize = maxColSize;
      }


      /* Check to see if the current row has a max column size that
         is larger than what we've been using so far.  If so, allocate
         new result strings, copy each row value over, and free the 
         old one. */
      if (maxColSize > currentMaxColSize) {
	 maxColSize += MINIMUM_COL_SIZE; /* bump it up to try to avoid
					    some multiple resizes */
	 if (debug) printf("Bumping %d to %d\n",
			   currentMaxColSize, maxColSize);
	 attriTextLen= numOfCols * maxColSize;
	 if (debug) printf("attriTextLen=%d\n",attriTextLen);
	 totalLen = attriTextLen * genQueryInp.maxRows;
	 for (j=0;j<numOfCols;j++) {
	    char *cp1, *cp2;
	    int k;
	    tResult = malloc(totalLen);
	    if (tResult==NULL) return(SYS_MALLOC_ERR);
	    memset(tResult, 0, totalLen);
	    cp1 = result->sqlResult[j].value;
	    cp2 = tResult;
	    for (k=0;k<result->rowCnt;k++) {
	       strncpy(cp2, cp1, result->sqlResult[j].len);
	       cp1 += result->sqlResult[j].len;
	       cp2 += maxColSize;
	    }
	    free(result->sqlResult[j].value);
	    result->sqlResult[j].len = maxColSize;
	    result->sqlResult[j].value = tResult;
	 }
	 currentMaxColSize = maxColSize;
      }

      /* Store the current row values into the appropriate spots in
         the attribute string */
      for (j=0;j<numOfCols;j++) {
	 tResult2 = result->sqlResult[j].value; /* ptr to value str */
	 tResult2 += currentMaxColSize*(result->rowCnt-1);  /* skip forward 
							for this row */
	 strncpy(tResult2, icss->stmtPtr[statementNum]->resultValue[j],
		 currentMaxColSize); /* copy in the value text */
      }

   }

   result->continueInx=statementNum+1;  /* the statementnumber but
						  always >0 */
   return(0);

}

int
chlDebugGenQuery(int mode) {
   logSQLGenQuery = mode;
   return(0);
}
