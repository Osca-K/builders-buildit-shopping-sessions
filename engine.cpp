// Exact CSV -> compact records -> sessions and rolling campaign attribution.
// Build: g++ -std=c++20 -O3 -DNDEBUG engine.cpp -o engine
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
namespace fs=std::filesystem;
using S=std::string_view;using U=uint32_t;using I=int64_t;using Q=uint64_t;
constexpr I DAY=86400000,WEEK=604800000,GAP=1800000;constexpr U NONE=UINT32_MAX;
template<class V>using Map=std::unordered_map<S,V>;
bool lessbytes(S a,S b){auto n=std::min(a.size(),b.size());int c=n?std::memcmp(a.data(),b.data(),n):0;return c<0||(c==0&&a.size()<b.size());}
struct File{
 int fd=-1;const char*data=nullptr;size_t size=0;
 File(const fs::path&p){fd=open(p.c_str(),O_RDONLY);if(fd<0)throw std::runtime_error("open failed");struct stat st{};if(fstat(fd,&st)||st.st_size<0)throw std::runtime_error("stat failed");size=st.st_size;if(size){auto q=mmap(nullptr,size,PROT_READ,MAP_PRIVATE,fd,0);if(q==MAP_FAILED)throw std::runtime_error("mmap failed");data=(char*)q;}}
 ~File(){if(data)munmap((void*)data,size);if(fd>=0)close(fd);}
};
struct Input{
 std::vector<std::unique_ptr<File>>files;
 template<class F>void rows(const fs::path&p,F fn){auto f=std::make_unique<File>(p);auto ptr=f->data;auto size=f->size;files.push_back(std::move(f));size_t off=0;bool header=true;while(off<size){auto start=ptr+off;auto lf=(const char*)memchr(start,'\n',size-off);size_t n=lf?lf-start:size-off;off+=n+(lf?1:0);if(header){header=false;continue;}fn(S(start,n));}}
};
template<size_t N>bool parse(S row,std::array<S,N>&out){size_t begin=0;for(size_t i=0;i<N;i++){auto comma=row.find(',',begin);if(i+1<N){if(comma==S::npos)return false;}else if(comma!=S::npos)return false;size_t end=comma==S::npos?row.size():comma;S s=row.substr(begin,end-begin);if(!s.empty()&&s.front()=='"'){if(s.size()<2||s.back()!='"')return false;s.remove_prefix(1);s.remove_suffix(1);if(s.find('"')!=S::npos)return false;}else if(s.find('"')!=S::npos)return false;out[i]=s;begin=end+1;}return true;}
std::vector<fs::path>csvs(fs::path dir){std::vector<fs::path>r;if(fs::exists(dir))for(auto&e:fs::directory_iterator(dir))if(e.is_regular_file()&&e.path().extension()==".csv")r.push_back(e.path());std::sort(r.begin(),r.end());return r;}
bool digit(char c){return c>='0'&&c<='9';}
bool digs(S s,size_t p,size_t n,int&v){if(p+n>s.size())return false;v=0;for(size_t i=0;i<n;i++){char c=s[p+i];if(!digit(c))return false;v=v*10+c-'0';}return true;}
I civil(int y,unsigned m,unsigned d){y-=m<=2;int era=(y>=0?y:y-399)/400;unsigned yy=y-era*400,mp=m>2?m-3:m+9;return I(era)*146097+(yy*365+yy/4-yy/100+(153*mp+2)/5+d-1)-719468;}
bool ts(S s,I&v){if(s.empty())return false;if((s.size()!=20&&s.size()!=25)||s.find_first_not_of("0123456789")==S::npos){Q n=0;for(char c:s){if(!digit(c))return false;unsigned d=c-'0';if(n>(Q(INT64_MAX)-d)/10)return false;n=n*10+d;}v=n;return true;}if(s[4]!='-'||s[7]!='-'||s[10]!='T'||s[13]!=':'||s[16]!=':')return false;int y,m,d,h,mi,se;if(!digs(s,0,4,y)||!digs(s,5,2,m)||!digs(s,8,2,d)||!digs(s,11,2,h)||!digs(s,14,2,mi)||!digs(s,17,2,se))return false;static int md[]={31,28,31,30,31,30,31,31,30,31,30,31};if(m<1||m>12||d<1||d>md[m-1]+(m==2&&y%4==0&&(y%100!=0||y%400==0))||h>23||mi>59||se>59)return false;I offset=0;if(s.size()==20){if(s[19]!='Z')return false;}else{int oh,om;if((s[19]!='+'&&s[19]!='-')||s[22]!=':'||!digs(s,20,2,oh)||!digs(s,23,2,om)||oh>23||om>59)return false;offset=(oh*60+om)*(s[19]=='-'?-1:1);}v=civil(y,m,d)*DAY+I(h*3600+mi*60+se)*1000-offset*60000;return true;}
bool f1(S s,I&v){return s.size()==20&&s[19]=='Z'&&ts(s,v);}
bool price(S s,I&v){if(s.empty())return false;I n=0;for(char c:s){if(!digit(c))return false;n=n*10+c-'0';if(n>10000000)return false;}v=n;return n>0;}
I day(I ms){return ms/DAY-(ms%DAY<0);}
std::string jsonvalue(const std::string&t,const char*k){auto p=t.find('"'+std::string(k)+'"');if(p==std::string::npos)throw std::runtime_error("manifest key missing");p=t.find(':',p);if(p==std::string::npos)throw std::runtime_error("manifest malformed");p=t.find('"',p);auto e=t.find('"',p+1);if(p==std::string::npos||e==std::string::npos)throw std::runtime_error("manifest malformed");return t.substr(p+1,e-p-1);}
struct People{
 Map<U>ids;std::vector<S>names;std::vector<U>parent,size,minimum;
 U intern(S s){auto it=ids.find(s);if(it!=ids.end())return it->second;if(names.size()>=NONE)throw std::runtime_error("too many IDs");U id=names.size();ids.emplace(s,id);names.push_back(s);parent.push_back(id);size.push_back(1);minimum.push_back(id);return id;}
 U root(U x){U r=x;while(parent[r]!=r)r=parent[r];while(parent[x]!=x){U n=parent[x];parent[x]=r;x=n;}return r;}
 void unite(U a,U b){a=root(a);b=root(b);if(a==b)return;if(size[a]<size[b])std::swap(a,b);parent[b]=a;size[a]+=size[b];if(lessbytes(names[minimum[b]],names[minimum[a]]))minimum[a]=minimum[b];}
 U person(S s){return minimum[root(intern(s))];}
};
struct Categories{Map<U>ids;std::vector<S>names={S{}};U intern(S s){if(s.empty())return 0;auto it=ids.find(s);if(it!=ids.end())return it->second;U id=names.size();ids.emplace(s,id);names.push_back(s);return id;}bool less(U a,U b){if(!a)return false;if(!b)return true;return lessbytes(names[a],names[b]);}};
struct Campaign{S id,channel;};
struct Campaigns{Map<U>ids;std::vector<Campaign>values;U organic;void add(S id,S channel){if(id.empty())return;auto it=ids.find(id);if(it!=ids.end()){values[it->second].channel=channel;return;}U i=values.size();ids.emplace(id,i);values.push_back({id,channel});}void finish(){organic=values.size();values.push_back({"organic","organic"});}};
struct Raw{S row;I ingest;};struct Event{I ts,revenue;U person,category;};struct Session{I ts,revenue;U person;};struct Click{I ts;S id;U person,campaign;};struct Total{I sessions=0,purchases=0,revenue=0,assists=0;};
// A contiguous event-ID table avoids a node allocation and pointer chase for
// every event. Hash collisions always compare the complete decoded ID.
struct EventIndex{
 struct Slot{size_t hash=0;U index=NONE;};std::vector<Slot>slots;size_t used=0;
 static S key(S row){S id=row.substr(0,row.find(','));if(id.size()>=2&&id.front()=='"'){id.remove_prefix(1);id.remove_suffix(1);}return id;}
 void allocate(size_t count){size_t n=1024;while(n<count*10/7+1)n*=2;slots.resize(n);}
 void grow(std::vector<Raw>&rows){auto old=std::move(slots);slots.resize(old.size()*2);for(auto&s:old)if(s.index!=NONE){size_t p=s.hash&(slots.size()-1);while(slots[p].index!=NONE)p=(p+1)&(slots.size()-1);slots[p]=s;}}
 void add(S id,S row,I ingest,std::vector<Raw>&rows){if((used+1)*10>slots.size()*7)grow(rows);size_t h=std::hash<S>{}(id),p=h&(slots.size()-1);while(slots[p].index!=NONE){auto&s=slots[p];if(s.hash==h&&key(rows[s.index].row)==id){if(ingest<rows[s.index].ingest)rows[s.index]={row,ingest};return;}p=(p+1)&(slots.size()-1);}if(rows.size()>=NONE)throw std::runtime_error("too many events");slots[p]={h,U(rows.size())};rows.push_back({row,ingest});used++;}
};
#include "parallel_event_dedup.hpp"
#include "parallel_event_validate.hpp"
#include "parallel_clicks.hpp"
struct CSV{
 std::vector<char>buffer;std::ofstream out;
 CSV(fs::path p):buffer(1<<20){out.rdbuf()->pubsetbuf(buffer.data(),buffer.size());out.open(p,std::ios::binary|std::ios::trunc);if(!out)throw std::runtime_error("output open failed");}
 void raw(S s){out.write(s.data(),s.size());}void str(S s){out.put('"');size_t b=0;for(size_t i=0;i<s.size();i++)if(s[i]=='"'){raw(s.substr(b,i-b));raw("\"\"");b=i+1;}raw(s.substr(b));out.put('"');}void num(I n){char a[32];char*p=a+32;Q v=n<0?Q(-(n+1))+1:Q(n);do{*--p='0'+v%10;v/=10;}while(v);if(n<0)*--p='-';out.write(p,a+32-p);}void comma(){out.put(',');}void nl(){out.put('\n');}void finish(){out.flush();if(!out)throw std::runtime_error("output write failed");out.close();}
};
struct Timer{std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now(),last=start;void stage(const char*n){auto now=std::chrono::steady_clock::now();std::cerr<<n<<": "<<std::chrono::duration<double>(now-last).count()<<"s total="<<std::chrono::duration<double>(now-start).count()<<"s\n";last=now;}};
int main(int argc,char**argv){try{
 if(argc!=3)throw std::runtime_error("usage: engine DATA_DIR SCRATCH_DIR");fs::path data=argv[1],scratch=argv[2];fs::create_directories(scratch);Timer timer;
 std::ifstream mf(data/"manifest.json");std::string manifest((std::istreambuf_iterator<char>(mf)),{});I start,end;if(!f1(jsonvalue(manifest,"window_start"),start)||!f1(jsonvalue(manifest,"window_end"),end)||end<=start)throw std::runtime_error("invalid window");
 Input input;People people;Categories categories;Campaigns campaigns;Map<U>products;U unknown=categories.intern("unknown");
 input.rows(data/"products.csv",[&](S row){std::array<S,2>f;if(parse(row,f)&&!f[0].empty())products.insert_or_assign(f[0],categories.intern(f[1]));});
 input.rows(data/"campaigns.csv",[&](S row){std::array<S,2>f;if(parse(row,f))campaigns.add(f[0],f[1]);});campaigns.finish();
 for(auto&p:csvs(data/"links"))input.rows(p,[&](S row){std::array<S,2>f;if(parse(row,f)&&!f[0].empty()&&!f[1].empty()){U a=people.intern(f[0]),b=people.intern(f[1]);people.unite(a,b);}});timer.stage("identities");
 unsigned ncpu=8;if(const char*env=std::getenv("NCPU"))ncpu=std::max(1,std::atoi(env));
 std::vector<Raw>raws=deduplicate_parallel(input,csvs(data/"events"),ncpu);timer.stage("event dedup");
 std::vector<Event>events=validate_events_parallel(raws,people,products,unknown,start,end,ncpu);std::vector<Raw>().swap(raws);timer.stage("event validate");
 std::sort(events.begin(),events.end(),[](auto&a,auto&b){return a.person!=b.person?a.person<b.person:a.ts<b.ts;});timer.stage("event sort");
 CSV users(scratch/"user_daily.csv");users.raw("user_id,day,sessions,events,revenue_cents,top_category\n");std::vector<Session>sessions;sessions.reserve(events.size()/4+1);std::vector<I>revenues(categories.names.size(),0);std::vector<U>touched;
 size_t begin=0;while(begin<events.size()){U person=events[begin].person;size_t finish=begin+1;while(finish<events.size()&&events[finish].person==person)finish++;bool have=false;I d=0,ns=0,ne=0,nr=0;
 auto flush=[&](){if(!have)return;U top=NONE;for(U c:touched)if(top==NONE||revenues[c]>revenues[top]||(revenues[c]==revenues[top]&&categories.less(c,top)))top=c;users.str(people.names[person]);users.comma();users.num(d);users.comma();users.num(ns);users.comma();users.num(ne);users.comma();users.num(nr);users.comma();if(top!=NONE&&top)users.str(categories.names[top]);users.nl();for(U c:touched)revenues[c]=0;touched.clear();};
 I prev=0;for(size_t i=begin;i<finish;i++){auto&e=events[i];if(i==begin||e.ts-prev>GAP){I sd=day(e.ts);if(!have||sd!=d){flush();have=true;d=sd;ns=ne=nr=0;}ns++;sessions.push_back({e.ts,0,person});}ne++;nr+=e.revenue;sessions.back().revenue+=e.revenue;if(e.revenue){if(!revenues[e.category])touched.push_back(e.category);revenues[e.category]+=e.revenue;}prev=e.ts;}flush();begin=finish;}
 users.finish();std::vector<Event>().swap(events);timer.stage("session aggregation");
 std::vector<Click>clicks=parse_clicks_parallel(input,csvs(data/"clicks"),people,campaigns,start,end,ncpu);timer.stage("click validate");
 std::sort(clicks.begin(),clicks.end(),[](auto&a,auto&b){if(a.person!=b.person)return a.person<b.person;if(a.ts!=b.ts)return a.ts<b.ts;return lessbytes(a.id,b.id);});timer.stage("click sort");
 std::vector<std::unordered_map<I,Total>>totals(campaigns.values.size());std::vector<Q>counts(campaigns.values.size(),0);std::vector<U>positions(campaigns.values.size(),NONE),active;
 auto add=[&](U c){if(counts[c]++==0){positions[c]=active.size();active.push_back(c);}};auto remove=[&](U c){if(--counts[c]==0){U p=positions[c],replacement=active.back();active[p]=replacement;positions[replacement]=p;active.pop_back();positions[c]=NONE;}};
 begin=0;size_t clickcursor=0;while(begin<sessions.size()){U person=sessions[begin].person;size_t finish=begin+1;while(finish<sessions.size()&&sessions[finish].person==person)finish++;while(clickcursor<clicks.size()&&clicks[clickcursor].person<person)clickcursor++;size_t cb=clickcursor,ce=cb;while(ce<clicks.size()&&clicks[ce].person==person)ce++;clickcursor=ce;size_t left=cb,right=cb,best=ce;
 for(size_t si=begin;si<finish;si++){auto&s=sessions[si];I earliest=s.ts-WEEK;while(right<ce&&clicks[right].ts<=s.ts){if(best==ce||clicks[right].ts>clicks[best].ts)best=right;add(clicks[right].campaign);right++;}while(left<right&&clicks[left].ts<earliest){remove(clicks[left].campaign);left++;}I d=day(s.ts);U credited=best!=ce&&clicks[best].ts>=earliest?clicks[best].campaign:campaigns.organic;auto&t=totals[credited][d];t.sessions++;t.purchases+=s.revenue>0;t.revenue+=s.revenue;for(U c:active)totals[c][d].assists++;}
 for(U c:active){counts[c]=0;positions[c]=NONE;}active.clear();begin=finish;}timer.stage("attribution assists");
 CSV output(scratch/"campaign_daily.csv");output.raw("campaign_id,channel,day,sessions,purchasing_sessions,revenue_cents,assisted_sessions\n");for(U c=0;c<campaigns.values.size();c++){auto&info=campaigns.values[c];for(auto&[d,t]:totals[c]){output.str(info.id);output.comma();if(!info.channel.empty())output.str(info.channel);output.comma();output.num(d);output.comma();output.num(t.sessions);output.comma();output.num(t.purchases);output.comma();output.num(t.revenue);output.comma();output.num(t.assists);output.nl();}}output.finish();timer.stage("output");return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
