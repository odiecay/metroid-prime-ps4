// Verify actual SQLite rename protection, with native-style zero-inode stat.
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
extern int prime_ps4_sqlite_init(void);
static int zero_identity(const char *path,struct stat *out) {
    int rc=stat(path,out);
    if(!rc) {out->st_ino=0;out->st_dev=0;}
    return rc;
}
int main(int argc,char **argv) {
    if(argc!=2) return 1;
    char file[1024],moved[1024];
    snprintf(file,sizeof(file),"%s/cache.db",argv[1]);
    snprintf(moved,sizeof(moved),"%s/moved.db",argv[1]);
    sqlite3 *db=NULL;
    if(sqlite3_open(file,&db) || sqlite3_exec(db,"PRAGMA user_version=1;",NULL,NULL,NULL)) return 15;
    sqlite3_close(db);db=NULL;
    sqlite3_vfs *vfs=sqlite3_vfs_find("unix");
    if(!vfs || !vfs->xSetSystemCall || vfs->xSetSystemCall(vfs,"stat",(sqlite3_syscall_ptr)zero_identity)) return 2;
    if(sqlite3_open(file,&db)) return 3;
    int rc=sqlite3_exec(db,"PRAGMA user_version=37;",NULL,NULL,NULL);
    if(rc!=SQLITE_READONLY || sqlite3_extended_errcode(db)!=SQLITE_READONLY_DBMOVED) {
        fprintf(stderr,"Broken identity result=%d extended=%d: %s\n",rc,sqlite3_extended_errcode(db),sqlite3_errmsg(db));
        return 4;
    }
    sqlite3_close(db);db=NULL;
    if(prime_ps4_sqlite_init()!=SQLITE_OK || sqlite3_open(file,&db)) return 5;
    rc=sqlite3_exec(db,"PRAGMA auto_vacuum=INCREMENTAL; PRAGMA journal_mode=WAL; CREATE TABLE test(value); INSERT INTO test VALUES(37);",NULL,NULL,NULL);
    if(rc) {fprintf(stderr,"%s\n",sqlite3_errmsg(db));return 6;}
    sqlite3_close(db);db=NULL;
    if(sqlite3_open(file,&db)) return 7;
    sqlite3_stmt *stmt=NULL;
    if(sqlite3_prepare_v2(db,"SELECT value FROM test",-1,&stmt,NULL)) return 8;
    if(sqlite3_step(stmt)!=SQLITE_ROW || sqlite3_column_int(stmt,0)!=37) return 9;
    sqlite3_finalize(stmt);
    int hasMoved=-1;
    if(sqlite3_file_control(db,"main",SQLITE_FCNTL_HAS_MOVED,&hasMoved) || hasMoved) return 10;
    if(rename(file,moved)) return 11;
    if(sqlite3_file_control(db,"main",SQLITE_FCNTL_HAS_MOVED,&hasMoved) || !hasMoved) return 12;
    if(unlink(moved)) return 13;
    if(sqlite3_file_control(db,"main",SQLITE_FCNTL_HAS_MOVED,&hasMoved) || !hasMoved) return 14;
    sqlite3_close(db);
    puts("DBMOVED reproduced; native identity adapter writes/reopens WAL and detects real rename/deletion");
}
