// SPDX-License-Identifier: MIT
// Actual native locking, SQLite WAL/rollback and reopen checks in private test
// files. No game save or existing cache is opened or changed.
#include <sqlite3.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <orbis/libkernel.h>

static FILE *report;
static int okay=1;
static void check(const char *stage,int passed) {
    fprintf(report,"{\"stage\":\"%s\",\"passed\":%s}\n",stage,passed?"true":"false");
    fflush(report);if(!passed) okay=0;
}
static int sql(sqlite3 *db,const char *text) {
    int result=sqlite3_exec(db,text,NULL,NULL,NULL);
    if(result!=SQLITE_OK) fprintf(report,"{\"sqlite_error\":%d}\n",result);
    return result==SQLITE_OK;
}
static int scalar(sqlite3 *db,const char *text,const char *expected) {
    sqlite3_stmt *stmt=NULL;
    if(sqlite3_prepare_v2(db,text,-1,&stmt,NULL)!=SQLITE_OK) return 0;
    const int r=sqlite3_step(stmt);
    const unsigned char *value=r==SQLITE_ROW?sqlite3_column_text(stmt,0):NULL;
    const int match=value && !strcmp((const char *)value,expected);
    sqlite3_finalize(stmt);return match;
}
int main(void) {
    mkdir("/data/project-prime",0700);mkdir("/data/project-prime/diagnostics",0700);
    report=fopen("/data/project-prime/storage.jsonl","w");if(!report) return 1;
    fprintf(report,"{\"schema\":1,\"target\":\"orbis\",\"probe\":\"native_storage\"}\n");fflush(report);
    char base[192],lock_path[224],db_path[224];
    snprintf(base,sizeof(base),"/data/project-prime/diagnostics/storage-%llu",(unsigned long long)sceKernelGetProcessTime());
    snprintf(lock_path,sizeof(lock_path),"%s.lock",base);snprintf(db_path,sizeof(db_path),"%s.db",base);
    int fd=open(lock_path,O_RDWR|O_CREAT|O_EXCL,0600);
    check("create_private_lock_file",fd>=0);
    if(fd<0) goto done;
    struct flock range={.l_start=0,.l_len=1,.l_type=F_WRLCK,.l_whence=SEEK_SET};
    const int queried=fcntl(fd,F_GETLK,&range);
    check("native_flock_query_layout",queried==0 && range.l_type==F_UNLCK);
    range=(struct flock){.l_start=0,.l_len=1,.l_type=F_WRLCK,.l_whence=SEEK_SET};
    check("native_flock_write_lock",fcntl(fd,F_SETLK,&range)==0);
    range.l_type=F_UNLCK;check("native_flock_unlock",fcntl(fd,F_SETLK,&range)==0);
    static const char bytes[]="Tallon IV storage durability probe";
    check("native_write",write(fd,bytes,sizeof(bytes))==sizeof(bytes));
    check("native_fdatasync",fdatasync(fd)==0);close(fd);fd=-1;
    sqlite3 *a=NULL,*b=NULL;
    if(!okay) goto close_db;
    int opened=sqlite3_open_v2(db_path,&a,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,NULL);
    check("sqlite_open_native_vfs",opened==SQLITE_OK);if(opened!=SQLITE_OK) goto close_db;
    check("sqlite_actual_wal_mode",scalar(a,"PRAGMA journal_mode=WAL;","wal"));
    check("sqlite_full_sync",sql(a,"PRAGMA synchronous=FULL; CREATE TABLE prime_probe(id INTEGER PRIMARY KEY,value TEXT);"));
    opened=sqlite3_open_v2(db_path,&b,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,NULL);
    check("sqlite_second_connection",opened==SQLITE_OK);if(opened!=SQLITE_OK) goto close_db;
    check("sqlite_writer_begin",sql(a,"BEGIN IMMEDIATE; INSERT INTO prime_probe VALUES(1,'Tallon IV');"));
    check("sqlite_conflicting_writer_busy",sqlite3_exec(b,"BEGIN IMMEDIATE;",NULL,NULL,NULL)==SQLITE_BUSY);
    check("sqlite_commit",sql(a,"COMMIT;"));
    check("sqlite_rollback",sql(a,"BEGIN; INSERT INTO prime_probe VALUES(2,'not committed'); ROLLBACK;"));
    check("sqlite_second_connection_visibility",scalar(b,"SELECT count(*) FROM prime_probe;","1"));
    sqlite3_close(b);b=NULL;sqlite3_close(a);a=NULL;
    opened=sqlite3_open_v2(db_path,&a,SQLITE_OPEN_READWRITE|SQLITE_OPEN_FULLMUTEX,NULL);
    check("sqlite_reopen",opened==SQLITE_OK);
    if(opened==SQLITE_OK) {
        check("sqlite_persisted_content",scalar(a,"SELECT value FROM prime_probe WHERE id=1;","Tallon IV"));
        check("sqlite_integrity",scalar(a,"PRAGMA quick_check;","ok"));
    }
close_db:
    if(b) sqlite3_close(b);if(a) sqlite3_close(a);
    if(okay) {
        unlink(lock_path);unlink(db_path);
        char sidecar[240];
        snprintf(sidecar,sizeof(sidecar),"%s-wal",db_path);unlink(sidecar);
        snprintf(sidecar,sizeof(sidecar),"%s-shm",db_path);unlink(sidecar);
    }
done:
    if(fd>=0) close(fd);
    check("storage_probe_end",okay);fclose(report);return okay?0:1;
}
