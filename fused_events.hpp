#pragma once

// One sequential input pass produces decoded event candidates. Invalid rows
// remain tombstones until earliest-ingest deduplication has selected a copy.
inline std::vector<Event> fused_events(Input&input,const std::vector<fs::path>&paths,
 People&people,const Map<U>&products,U unknown,I start,I end,unsigned requested){
 constexpr unsigned SHARDS=32,BITS=64;
 unsigned threads=std::max(1u,std::min({requested,std::max(1u,std::thread::hardware_concurrency()),8u}));
 if(people.names.size()>=NONE)throw std::runtime_error("too many identities");
 U base=people.names.size();std::vector<U>canonical(base);
 for(U id=0;id<base;++id)canonical[id]=people.minimum[people.root(id)];
 const auto&existing=static_cast<const PackedIds&>(people.ids);
 struct Candidate{Q code;I ingest,ts;U person,revenue,category,long_id;};
 static_assert(sizeof(Candidate)==40);
 struct Local{std::array<std::vector<Candidate>,SHARDS>buckets;PackedIds ids;std::vector<S>names,long_keys;std::vector<U>remap;U key_base=0;};
 std::vector<Local>local(threads);
 struct Chunk{const char*first,*last;};std::vector<Chunk>chunks;
 constexpr size_t TARGET=4u*1024*1024;
 for(const auto&path:paths){auto file=std::make_unique<File>(path);auto*data=file->data;size_t size=file->size;input.files.push_back(std::move(file));
  if(!size)continue;const char*last=data+size,*header=(const char*)memchr(data,'\n',size);if(!header)continue;
  const char*first=header+1;while(first<last){const char*finish=last;
   if(size_t(last-first)>TARGET){const char*lf=(const char*)memchr(first+TARGET,'\n',last-first-TARGET);if(lf)finish=lf+1;}
   chunks.push_back({first,finish});first=finish;
  }
 }
 auto parallel=[&](auto body){std::atomic<bool>cancelled{false};std::exception_ptr failure;std::mutex mutex;std::vector<std::jthread>workers;
  try{for(unsigned tid=0;tid<threads;++tid)workers.emplace_back([&,tid]{try{body(tid,cancelled);}catch(...){std::lock_guard<std::mutex>lock(mutex);if(!failure)failure=std::current_exception();cancelled.store(true,std::memory_order_relaxed);}});}
  catch(...){cancelled.store(true,std::memory_order_relaxed);workers.clear();throw;}
  for(auto&worker:workers)worker.join();if(failure)std::rethrow_exception(failure);
 };
 std::atomic<size_t>next_chunk{0};
 parallel([&](unsigned tid,std::atomic<bool>&cancelled){auto&part=local[tid];
  for(;;){if(cancelled.load(std::memory_order_relaxed))return;size_t job=next_chunk.fetch_add(1,std::memory_order_relaxed);if(job>=chunks.size())break;
   const char*p=chunks[job].first,*last=chunks[job].last;unsigned checked=0;
   while(p<last){if((++checked&4095u)==0&&cancelled.load(std::memory_order_relaxed))return;
    const char*lf=(const char*)memchr(p,'\n',last-p),*finish=lf?lf:last;S row(p,finish-p);p=lf?lf+1:last;
    std::array<S,7>fields;I ingest;if(!parse(row,fields)||fields[0].empty()||!f1(fields[3],ingest))continue;
    Candidate candidate{PackedIds::key(fields[0]),ingest,0,NONE,0,0,0};
    Q hash=candidate.code?ankerl::unordered_dense::hash<Q>{}(candidate.code):ankerl::unordered_dense::hash<S>{}(fields[0]);
    if(!candidate.code){if(part.long_keys.size()>=NONE)throw std::runtime_error("too many long event IDs");candidate.long_id=part.long_keys.size();part.long_keys.push_back(fields[0]);}
    bool purchase=fields[4]=="purchase";I timestamp,revenue=0;
    if(!fields[1].empty()&&!fields[5].empty()&&(purchase||fields[4]=="view"||fields[4]=="cart")&&
       ts(fields[2],timestamp)&&timestamp>=start&&timestamp<end&&ingest-timestamp<=DAY&&(!purchase||price(fields[6],revenue))){
     candidate.ts=timestamp;candidate.revenue=U(revenue);
     if(purchase){auto product=products.find(fields[5]);candidate.category=product==products.end()?unknown:product->second;}
     U person=existing.get(fields[1]);if(person!=NONE)candidate.person=canonical[person];
     else{person=part.ids.get(fields[1]);if(person==NONE){if(part.names.size()>=NONE-base)throw std::runtime_error("too many identities");person=base+part.names.size();part.ids.emplace(fields[1],person);part.names.push_back(fields[1]);}candidate.person=person;}
    }
    part.buckets[hash>>(BITS-5)].push_back(candidate);
   }
  }
 });
 std::vector<S>long_keys;size_t long_count=0;for(auto&part:local)long_count+=part.long_keys.size();if(long_count>=NONE)throw std::runtime_error("too many long event IDs");long_keys.reserve(long_count);
 for(auto&part:local){part.key_base=long_keys.size();long_keys.insert(long_keys.end(),part.long_keys.begin(),part.long_keys.end());std::vector<S>().swap(part.long_keys);
  part.ids.clear();part.remap.reserve(part.names.size());for(S name:part.names)part.remap.push_back(people.person(name));std::vector<S>().swap(part.names);
 }
 struct Slot{Q fingerprint=0;U index=NONE;bool packed=false;};static_assert(sizeof(Slot)==16);
 std::array<std::vector<Event>,SHARDS>events;std::atomic<unsigned>next_shard{0};
 parallel([&](unsigned,std::atomic<bool>&cancelled){for(;;){if(cancelled.load(std::memory_order_relaxed))return;unsigned shard=next_shard.fetch_add(1,std::memory_order_relaxed);if(shard>=SHARDS)break;
   size_t count=0;for(auto&part:local)count+=part.buckets[shard].size();if(!count)continue;
   size_t capacity=1024;while(capacity<count*10/7+1)capacity*=2;std::vector<Slot>slots(capacity);std::vector<Candidate>selected;selected.reserve(count);
   for(auto&part:local){for(auto candidate:part.buckets[shard]){
     if(!candidate.code)candidate.long_id+=part.key_base;
     if(candidate.person!=NONE&&candidate.person>=base)candidate.person=part.remap[candidate.person-base];
     Q hash=candidate.code?ankerl::unordered_dense::hash<Q>{}(candidate.code):ankerl::unordered_dense::hash<S>{}(long_keys[candidate.long_id]);
     Q fingerprint=candidate.code?candidate.code:hash;size_t position=hash&(capacity-1);
     while(slots[position].index!=NONE){auto&slot=slots[position];
      if(slot.fingerprint==fingerprint&&slot.packed==bool(candidate.code)&&
         (candidate.code||long_keys[selected[slot.index].long_id]==long_keys[candidate.long_id])){
       if(candidate.ingest<selected[slot.index].ingest)selected[slot.index]=candidate;goto deduplicated;
      }position=(position+1)&(capacity-1);
     }
     if(selected.size()>=NONE)throw std::runtime_error("too many events");slots[position]={fingerprint,U(selected.size()),bool(candidate.code)};selected.push_back(candidate);
     deduplicated:;
    }std::vector<Candidate>().swap(part.buckets[shard]);}
   auto&out=events[shard];out.reserve(selected.size());for(const auto&candidate:selected)if(candidate.person!=NONE)out.push_back({candidate.ts,I(candidate.revenue),candidate.person,candidate.category});
  }
 });
 size_t count=0;for(const auto&part:events)count+=part.size();std::vector<Event>result;result.reserve(count);
 for(auto&part:events){result.insert(result.end(),part.begin(),part.end());std::vector<Event>().swap(part);}return result;
}
