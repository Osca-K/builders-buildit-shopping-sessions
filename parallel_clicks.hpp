#pragma once

// Duplicate click IDs are guaranteed to have identical parsed values. Keeping
// copies changes neither latest-click attribution nor campaign presence. Each
// retained copy enters and leaves the rolling counts exactly once.
inline std::vector<Click> parse_clicks_parallel(Input&input,const std::vector<fs::path>&paths,People&people,const Campaigns&campaigns,I start,I end,unsigned requested){
 unsigned threads=std::max(1u,std::min({requested,std::max(1u,std::thread::hardware_concurrency()),8u}));
 std::vector<U>canonical(people.names.size());for(U id=0;id<canonical.size();id++)canonical[id]=people.minimum[people.root(id)];
 const auto&existing=static_cast<const Map<U>&>(people.ids);
 std::vector<File*>mapped;for(auto&p:paths){auto f=std::make_unique<File>(p);mapped.push_back(f.get());input.files.push_back(std::move(f));}
 std::vector<std::vector<Click>>parts(paths.size());std::atomic<size_t>next{0};std::atomic<bool>cancelled{false};std::exception_ptr failure;std::mutex mutex;std::vector<std::jthread>workers;
 auto worker=[&]{try{while(!cancelled.load(std::memory_order_relaxed)){
  size_t fi=next.fetch_add(1,std::memory_order_relaxed);if(fi>=mapped.size())break;auto&file=*mapped[fi];if(!file.size)continue;
  auto&out=parts[fi];out.reserve(file.size/90);const char*p=file.data,*endp=p+file.size,*lf=(const char*)memchr(p,'\n',endp-p);if(!lf)continue;p=lf+1;
  while(p<endp){lf=(const char*)memchr(p,'\n',endp-p);const char*last=lf?lf:endp;S row(p,last-p);p=lf?lf+1:endp;std::array<S,4>f;
   if(!parse(row,f)||f[0].empty()||f[1].empty())continue;auto person=existing.find(f[1]);if(person==existing.end())continue;
   auto campaign=campaigns.ids.find(f[2]);I t;if(campaign==campaigns.ids.end()||!ts(f[3],t)||t<start-WEEK||t>=end)continue;
   out.push_back({t,f[0],canonical[person->second],campaign->second});
  }
 }}catch(...){std::lock_guard<std::mutex>lock(mutex);if(!failure)failure=std::current_exception();cancelled.store(true,std::memory_order_relaxed);}};
 try{for(unsigned i=0;i<threads;i++)workers.emplace_back(worker);}catch(...){cancelled.store(true,std::memory_order_relaxed);workers.clear();throw;}
 for(auto&w:workers)w.join();if(failure)std::rethrow_exception(failure);
 size_t total=0;for(auto&p:parts)total+=p.size();std::vector<Click>result;result.reserve(total);for(auto&p:parts){result.insert(result.end(),p.begin(),p.end());std::vector<Click>().swap(p);}return result;
}
