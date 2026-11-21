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
 std::vector<U> jobs;for(U p=0;p<people;++p)if(offsets[p+1]-offsets[p]>1)jobs.push_back(p);
 // Schedule the largest histories first so a late whale cannot stall the tail.
 std::sort(jobs.begin(),jobs.end(),[&](U a,U b){return offsets[a+1]-offsets[a]>offsets[b+1]-offsets[b];});
 unsigned threads=std::max(1u,std::min({requested,std::max(1u,std::thread::hardware_concurrency()),8u}));
 std::atomic<size_t> next{0};std::vector<std::jthread>workers;
 for(unsigned t=0;t<threads;++t)workers.emplace_back([&]{
  for(;;){size_t job=next.fetch_add(1,std::memory_order_relaxed);if(job>=jobs.size())break;
   U p=jobs[job];std::sort(records.begin()+offsets[p],records.begin()+offsets[p+1],compare);
  }
 });
 for(auto&w:workers)w.join();
}
