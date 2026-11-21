#pragma once
#include <atomic>
#include <exception>
#include <mutex>
#include <thread>

// All string views remain owned by Input's mapped files. Each file and each
// hash shard has a single writer; the merge order is independent of scheduling.
inline std::vector<Raw> deduplicate_parallel(Input& input, const std::vector<fs::path>& paths, unsigned requested) {
 constexpr unsigned SHARDS=32, BITS=std::numeric_limits<size_t>::digits;
 unsigned threads=std::max(1u,std::min({requested,std::max(1u,std::thread::hardware_concurrency()),8u}));
 std::vector<File*> mapped;
 for(auto&path:paths){auto f=std::make_unique<File>(path);mapped.push_back(f.get());input.files.push_back(std::move(f));}
 using Buckets=std::array<std::vector<Raw>,SHARDS>;
 std::vector<Buckets> buckets(mapped.size());
 auto parallel=[&](auto body){
  std::atomic<bool> cancelled{false};std::exception_ptr failure;std::mutex mutex;std::vector<std::jthread> workers;
  try{for(unsigned tid=0;tid<threads;tid++)workers.emplace_back([&,tid]{try{body(tid,cancelled);}catch(...){std::lock_guard<std::mutex> lock(mutex);if(!failure)failure=std::current_exception();cancelled.store(true,std::memory_order_relaxed);}});}
  catch(...){cancelled.store(true,std::memory_order_relaxed);workers.clear();throw;}
  for(auto&w:workers)w.join();if(failure)std::rethrow_exception(failure);
 };
 std::atomic<size_t> next_file{0};
 parallel([&](unsigned,std::atomic<bool>&cancelled){
  while(!cancelled.load(std::memory_order_relaxed)){
   size_t fi=next_file.fetch_add(1,std::memory_order_relaxed);if(fi>=mapped.size())break;
   auto&file=*mapped[fi];if(!file.size)continue;
   const char*p=file.data,*end=p+file.size,*lf=(const char*)memchr(p,'\n',end-p);if(!lf)continue;p=lf+1;
   while(p<end){
    if(cancelled.load(std::memory_order_relaxed))break;
    lf=(const char*)memchr(p,'\n',end-p);const char*last=lf?lf:end;S row(p,last-p);p=lf?lf+1:end;
    std::array<S,7> f;I ingest;Q layout;if(!parse(row,f,&layout)||f[0].empty()||!f1(f[3],ingest))continue;
    unsigned shard=event_hash(f[0])>>(BITS-5);buckets[fi][shard].push_back({row.data(),ingest,layout});
   }
   for(auto&b:buckets[fi])if(b.capacity()>b.size()){std::vector<Raw> compact(b.begin(),b.end());b.swap(compact);}
  }
 });
 std::array<std::vector<Raw>,SHARDS> unique;std::atomic<unsigned> next_shard{0};
 parallel([&](unsigned,std::atomic<bool>&cancelled){
  while(!cancelled.load(std::memory_order_relaxed)){
   unsigned shard=next_shard.fetch_add(1,std::memory_order_relaxed);if(shard>=SHARDS)break;
   size_t count=0;for(auto&b:buckets)count+=b[shard].size();if(!count)continue;
   EventIndex index;index.allocate(count);auto&out=unique[shard];out.reserve(count);
   for(auto&b:buckets){for(auto&r:b[shard])index.add(r,out);std::vector<Raw>().swap(b[shard]);}
  }
 });
 size_t total=0;for(auto&s:unique)total+=s.size();std::vector<Raw> result;result.reserve(total);
 for(auto&s:unique){result.insert(result.end(),s.begin(),s.end());std::vector<Raw>().swap(s);}return result;
}
