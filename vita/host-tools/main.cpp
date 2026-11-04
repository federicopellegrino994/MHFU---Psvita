#include "install/executable_preparation.hpp"
#include "data_bin.hpp"
#include <fstream>
#include <filesystem>
#include <iterator>
#include <iostream>
using namespace mhf::launcher::install;
Bytes read(const std::string &p) { std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("cannot read "+p);return {std::istreambuf_iterator<char>(f),{}}; }
int main(int argc,char**argv) {try {
 if(argc!=4)return 2;
 ExecutableIdentity id{1910384,"e7ad91caa8afe7840c61a39c90d491e3779c35c7",1910384,"04b60ee797bd628faa0525ebd52186fa05967963"};
 auto result=prepare_executable(read(argv[1]),id,builtin_decryptor());
 if(result.status!=PrepareStatus::Ok)throw std::runtime_error("executable verification/preparation failed");
 std::filesystem::create_directories(argv[3]);auto out=std::filesystem::path(argv[3]);
 std::ofstream(out/"prepared.elf",std::ios::binary).write((char*)result.executable.data(),result.executable.size());
 std::ifstream data(argv[2],std::ios::binary);data.seekg(0,std::ios::end);auto length=data.tellg();
 auto reader=[&](std::uint64_t offset,std::size_t size){Bytes b(size);data.clear();data.seekg(offset);data.read((char*)b.data(),size);if(!data)throw std::runtime_error("container read failed");return b;};
 auto bin=mhf::mhfu::DataBin::open(reader,length,mhf::mhfu::find_substitution_table_candidates(result.executable));
 if(!bin)throw std::runtime_error("container decoding failed");
 unsigned count=0;std::uint64_t total=0;
 for(auto &f:bin->files()) {if(f.size<64)continue;auto h=mhf::mhfu::parse_overlay_header(bin->read(f.id,64));if(!h||h->file_size()>f.size)continue;
 auto b=bin->read(f.id,h->file_size());std::string name=h->name;
 if(name.find('/')!=std::string::npos||name.find('\\')!=std::string::npos||name==".."||name.empty())throw std::runtime_error("invalid overlay name");
 std::ofstream(out/name,std::ios::binary).write((char*)b.data(),b.size());++count;total+=b.size();
 }
 std::cout<<"Executable prepared and SHA1 verified\nContainer entries: "<<bin->files().size()<<"\nOverlays extracted: "<<count<<"\nOverlay bytes: "<<total<<"\n";
 if(count!=298) return 1;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
