#define main pipeline_main
#include "../engine.cpp"
#undef main
#include <random>
#include <cassert>
#include "timestamp_reference.hpp"
#include "csv_reference.hpp"

int main(){
 Map<U> ids;
 std::vector<std::string> owned;
 std::mt19937 rng(42);
 owned.reserve(12000);
 // Include length boundaries, zero bytes, UTF-8 and independently owned copies.
 for(size_t length=0;length<64;++length){
  for(unsigned n=0;n<150;++n){
   std::string key(length,'\0');
   for(auto&c:key)c=char(rng()&255);
   owned.push_back(key);
   ids.insert_or_assign(S(owned.back()),U(owned.size()));
  }
 }
 for(auto&key:owned){
  std::string copy=key;
  auto a=ids.find(S(key)),b=ids.find(S(copy));
  assert(a!=ids.end()&&b!=ids.end()&&a->second==b->second);
  InlineKey original{S(key)},duplicate{S(copy)};
  assert(original==duplicate);
  assert(InlineKeyHash{}(original)==InlineKeyHash{}(duplicate));
  if(!copy.empty()){
   copy.back()^=1;
   assert(!(original==InlineKey(S(copy))));
  }
 }
 // UTF-8 bytes are compared without character normalization.
 std::string utf8="\xc3\xa9\xe6\x97\xa5\xf0\x9f\x90\x88";
 assert(InlineKey(utf8)==InlineKey(std::string(utf8)));
 assert(!(InlineKey(S("u",1))==InlineKey(S("u\0",2))));
 auto check_ts=[](S text){I a=0,b=0;bool aa=ts(text,a),bb=reference_ts(text,b);assert(aa==bb);if(aa)assert(a==b);};
 for(auto base:{"2026-10-03T23:58:10Z","2028-02-29T23:58:10-04:00","00017910718900000000","1791071890000000000000000"}){
  check_ts(base);
  for(size_t position=0;position<S(base).size();++position){
   for(unsigned byte=0;byte<256;++byte){std::string altered=base;altered[position]=char(byte);check_ts(altered);}
  }
 }
 for(size_t length=0;length<100;++length){
  for(unsigned attempt=0;attempt<1000;++attempt){std::string text(length,'0');for(auto&c:text)c=char('0'+rng()%10);check_ts(text);}
 }
 auto check_csv=[&]<size_t N>(S row){std::array<S,N>a,b;Q aa=0,bb=0;bool av=parse(row,a,&aa),bv=reference_parse(row,b,&bb);assert(av==bv);if(av){assert(a==b);assert(aa==bb);}};
 for(unsigned attempt=0;attempt<200000;++attempt){
  std::string row;
  for(unsigned field=0;field<7;++field){
   if(field)row+=',';bool quoted=rng()%3==0;if(quoted)row+='"';
   unsigned n=rng()%50;for(unsigned i=0;i<n;++i){unsigned c=rng()%256;if(c==','||c=='"')c='x';row+=char(c);}
   if(quoted)row+='"';
  }
  check_csv.template operator()<7>(row);
  for(unsigned mutation=0;mutation<3;++mutation){
   if(!row.empty())row[rng()%row.size()]=char(rng()%256);
   check_csv.template operator()<7>(row);check_csv.template operator()<4>(row);
  }
 }
 std::cout<<"identifier, timestamp and CSV differential checks passed\n";
}
