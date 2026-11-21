#define main pipeline_main
#include "../engine.cpp"
#undef main
#include <random>
#include <cassert>
#include <map>
#include "timestamp_reference.hpp"
#include "csv_reference.hpp"

int main(){
 Map<U> ids;
 PackedIds packed_ids;
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
   if(packed_ids.get(owned.back())==NONE)packed_ids.emplace(owned.back(),U(owned.size()));
  }
 }
 for(auto&key:owned){
  std::string copy=key;
  auto a=ids.find(S(key)),b=ids.find(S(copy));
  assert(a!=ids.end()&&b!=ids.end()&&a->second==b->second);
  assert(packed_ids.get(key)!=NONE&&packed_ids.get(key)==packed_ids.get(copy));
  InlineKey original{S(key)},duplicate{S(copy)};
  assert(original==duplicate);
  assert(InlineKeyHash{}(original)==InlineKeyHash{}(duplicate));
  if(!copy.empty()){
   copy.back()^=1;
   assert(!(original==InlineKey(S(copy))));
   Q pa=PackedIds::key(key),pb=PackedIds::key(copy);
   if(pa&&pb)assert(pa!=pb);
  }
 }
 // UTF-8 bytes are compared without character normalization.
 std::string utf8="\xc3\xa9\xe6\x97\xa5\xf0\x9f\x90\x88";
 assert(InlineKey(utf8)==InlineKey(std::string(utf8)));
 assert(!(InlineKey(S("u",1))==InlineKey(S("u\0",2))));
 assert(PackedIds::key(S("u",1))!=PackedIds::key(S("u\0",2)));
 for(unsigned first=0;first<128;++first)for(unsigned last=0;last<128;++last){
  std::string text(9,'a');text[0]=char(first);text[8]=char(last);
  Q code=PackedIds::key(text);assert(code);Q decoded=code&~(Q(1)<<63);
  for(unsigned i=0;i<9;++i)assert(((decoded>>(i*7))&127)==static_cast<unsigned char>(text[i]));
 }
 {
  std::vector<std::string> backing;backing.reserve(40000);
  std::map<std::string,I> expected;EventIndex index;index.allocate(0);std::vector<Raw> selected;
  for(unsigned copy=0;copy<3;++copy)for(unsigned id=0;id<10000;++id){
   std::string key=(id%3==0?"long_fallback_identity_":id%3==1?"e":"\xc3\xa9")+std::to_string(id);
   I ingest=I(id)*10-I(copy);
   backing.push_back(key+",u,2026-10-01T00:00:00Z,2026-10-01T00:00:00Z,view,p,0");
   std::array<S,7> fields;Q layout;assert(parse(S(backing.back()),fields,&layout));
   index.add({backing.back().data(),ingest,layout},selected);
   auto [it,added]=expected.emplace(key,ingest);if(!added)it->second=std::min(it->second,ingest);
  }
  assert(selected.size()==expected.size());
  for(const auto&raw:selected)assert(expected.at(std::string(raw.key()))==raw.ingest);
 }
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
 std::cout<<"identifier, timestamp, CSV and event index checks passed\n";
}
