#pragma once

inline std::vector<Event> validate_events_parallel(const std::vector<Raw>& raw,People& people,const Map<U>& products,U unknown,I start,I end,unsigned requested){
 unsigned threads=std::max(1u,std::min({requested,std::max(1u,std::thread::hardware_concurrency()),8u}));
 if(people.names.size()>=NONE)throw std::runtime_error("too many identities");U base=people.names.size();
 std::vector<U> canonical(base);for(U id=0;id<base;id++)canonical[id]=people.minimum[people.root(id)];
 const auto&existing=static_cast<const Map<U>&>(people.ids);
 struct alignas(64) Local{std::vector<Event>events;Map<U>ids;std::vector<S>names;};std::vector<Local> local(threads);
 std::atomic<bool>cancelled{false};std::exception_ptr failure;std::mutex mutex;std::vector<std::jthread> workers;
 auto worker=[&](unsigned tid){try{
  size_t q=raw.size()/threads,r=raw.size()%threads,a=q*tid+std::min<size_t>(tid,r),b=a+q+(tid<r);auto&out=local[tid];out.events.reserve(b-a);
  for(size_t i=a;i<b;i++){
   if(((i-a)&4095u)==0&&cancelled.load(std::memory_order_relaxed))return;
   const auto&r=raw[i];std::array<S,7>f;if(!r.fields(f)||f[1].empty()||f[5].empty())continue;
   bool purchase=f[4]=="purchase";if(!purchase&&f[4]!="view"&&f[4]!="cart")continue;
   I t,revenue=0;if(!ts(f[2],t)||t<start||t>=end||r.ingest-t>DAY||(purchase&&!price(f[6],revenue)))continue;
   U category=0;if(purchase){auto p=products.find(f[5]);category=p==products.end()?unknown:p->second;}
   U person;auto known=existing.find(f[1]);if(known!=existing.end())person=canonical[known->second];
   else{auto it=out.ids.find(f[1]);if(it!=out.ids.end())person=it->second;else{if(out.names.size()>=NONE-base)throw std::runtime_error("too many identities");person=base+out.names.size();out.ids.emplace(f[1],person);out.names.push_back(f[1]);}}
   out.events.push_back({t,revenue,person,category});
  }
 }catch(...){std::lock_guard<std::mutex> lock(mutex);if(!failure)failure=std::current_exception();cancelled.store(true,std::memory_order_relaxed);}};
 try{for(unsigned tid=0;tid<threads;tid++)workers.emplace_back(worker,tid);}catch(...){cancelled.store(true,std::memory_order_relaxed);workers.clear();throw;}
 for(auto&w:workers)w.join();if(failure)std::rethrow_exception(failure);
 size_t total=0;for(auto&part:local){total+=part.events.size();Map<U>().swap(part.ids);}
 std::vector<Event>result;result.reserve(total);
 for(auto&part:local){std::vector<U>remap;remap.reserve(part.names.size());for(S name:part.names)remap.push_back(people.person(name));std::vector<S>().swap(part.names);
  for(auto&e:part.events)if(e.person>=base)e.person=remap[e.person-base];result.insert(result.end(),part.events.begin(),part.events.end());std::vector<Event>().swap(part.events);}
 return result;
}
