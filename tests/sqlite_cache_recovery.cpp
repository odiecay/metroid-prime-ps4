// Tests the production inline cache-opening helpers against real SQLite files.
int main(int argc,char** argv) {
  assert(argc==2);const fs::path dir=argv[1];fs::create_directories(dir);
  aurora::Module log;
  sqlite3* db=nullptr;const auto file=(dir/"cache.db").string();
  assert(aurora::sqlite::open_cache_db(file,&db,log)==SQLITE_OK);
  assert(aurora::sqlite::exec(db,"CREATE TABLE warm(value);INSERT INTO warm VALUES(42);")==SQLITE_OK);
  assert(sqlite3_close(db)==SQLITE_OK);
  assert(aurora::sqlite::open_cache_db(file,&db,log)==SQLITE_OK);
  int value=0;
  assert(aurora::sqlite::exec(db,"SELECT value FROM warm;",[&](int,char** row,char**) {value=std::atoi(row[0]);})==SQLITE_OK);
  assert(value==42);sqlite3_close(db);
  std::ofstream(dir/"save.dat")<<"progress must survive cache recovery";
  // Corrupt header, stale sidecars, then recreate only this disposable cache.
  std::ofstream(file,std::ios::trunc)<<"not a sqlite database";
  for(auto suffix:{"-wal","-shm","-journal"}) std::ofstream(file+suffix)<<"stale";
  assert(aurora::sqlite::open_cache_db(file,&db,log)==SQLITE_OK);
  assert(aurora::sqlite::exec(db,"CREATE TABLE rebuilt(value);")==SQLITE_OK);sqlite3_close(db);
  for(auto suffix:{"-wal","-shm","-journal"}) assert(!fs::exists(file+suffix));
  // A valid database whose body was cut off must be detected too.
  fs::resize_file(file,512);
  assert(aurora::sqlite::open_cache_db(file,&db,log)==SQLITE_OK);
  assert(aurora::sqlite::exec(db,"CREATE TABLE after_truncation(value);")==SQLITE_OK);sqlite3_close(db);
  assert(aurora::sqlite::open_cache_db(file,&db,log)==SQLITE_OK);
  assert(aurora::sqlite::exec(db,"BEGIN EXCLUSIVE;")==SQLITE_OK);
  sqlite3* locked=nullptr;
  const int result=aurora::sqlite::open_cache_db(file,&locked,log);
  assert(result!=SQLITE_OK && locked==nullptr && fs::exists(file));
  assert(aurora::sqlite::exec(db,"ROLLBACK;")==SQLITE_OK);sqlite3_close(db);
  const auto cannot=(dir/"directory-not-a-db");fs::create_directory(cannot);
  sqlite3* failed=nullptr;
  assert(aurora::sqlite::open_cache_db(cannot.string(),&failed,log)!=SQLITE_OK && !failed);
  assert(fs::is_directory(cannot));
  std::ifstream save(dir/"save.dat");std::string text;std::getline(save,text);
  assert(text=="progress must survive cache recovery");
  std::puts("Production cache helper: warm persistence, corrupt/truncated recovery, stale sidecars, lock/open failure and save isolation passed");
}
