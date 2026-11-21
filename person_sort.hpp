#pragma once

// Partition once by dense canonical person IDs, then sort independent histories.
// Unlike a global comparison sort, the expensive timestamp/ID comparison never
// compares records belonging to different people.
template<class Record,class Compare>
void sort_person_histories(std::vector<Record>& records,size_t people,unsigned requested,Compare compare){
 if(records.empty())return;
 std::vector<size_t> offsets(people+1,0);
 for(const auto&r:records)++offsets[r.person+1];
 for(size_t i=1;i<offsets.size();++i)offsets[i]+=offsets[i-1];
 auto cursor=offsets;std::vector<Record> grouped(records.size());
 for(const auto&r:records)grouped[cursor[r.person]++]=r;
 records.swap(grouped);std::vector<Record>().swap(grouped);
 std::vector<U> large,small;
 for(U p=0;p<people;++p){auto n=offsets[p+1]-offsets[p];if(n>256)large.push_back(p);else if(n>1)small.push_back(p);}
 // Schedule whales first. Small histories retain contiguous person order and
 // share one atomic claim per batch instead of one claim per person.
 std::sort(large.begin(),large.end(),[&](U a,U b){return offsets[a+1]-offsets[a]>offsets[b+1]-offsets[b];});
 unsigned threads=std::max(1u,std::min({requested,std::max(1u,std::thread::hardware_concurrency()),8u}));
 std::atomic<size_t> next{0},next_small{0};std::vector<std::jthread>workers;
 for(unsigned t=0;t<threads;++t)workers.emplace_back([&]{
  for(;;){size_t job=next.fetch_add(1,std::memory_order_relaxed);if(job>=large.size())break;
   U p=large[job];std::sort(records.begin()+offsets[p],records.begin()+offsets[p+1],compare);
  }
  for(;;){size_t job=next_small.fetch_add(64,std::memory_order_relaxed);if(job>=small.size())break;
   for(size_t j=job;j<std::min(job+64,small.size());++j){U p=small[j];std::sort(records.begin()+offsets[p],records.begin()+offsets[p+1],compare);}
  }
 });
 for(auto&w:workers)w.join();
}
