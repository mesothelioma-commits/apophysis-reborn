#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <algorithm>
struct Point {double x=0,y=0;};
Point variation(int id,Point p){double r2=p.x*p.x+p.y*p.y; switch(id){case 0:return p;case 1:return {sin(p.x),sin(p.y)};case 2:{double d=std::max(r2,1e-20);return {p.x/d,p.y/d};}case 3:{double s=sin(r2),c=cos(r2);return {p.x*s-p.y*c,p.x*c+p.y*s};}default:throw std::runtime_error("Unsupported variation");}}
struct Xform {double a,b,c,d,e,f,weight,color;int var;};
int main(int argc,char**argv){if(argc!=5){std::cerr<<"usage: flame-cpu width height samples output.ppm\n";return 2;}try{int w=std::stoi(argv[1]),h=std::stoi(argv[2]);auto n=std::stoull(argv[3]);if(w<1||h<1||w>16384||h>16384||n<1||n>1000000000ULL)throw std::runtime_error("Invalid dimensions or samples");std::vector<Xform> xs={{.5,0,0,.5,-.5,0,1,.1,0},{.5,0,0,.5,.5,0,1,.6,0},{.5,0,0,.5,0,.7,1,.95,0}};std::vector<uint32_t> count(size_t(w)*h,0);std::vector<double> hue(size_t(w)*h,0);std::mt19937_64 rng(123456);std::uniform_int_distribution<int> pick(0,int(xs.size())-1);Point p{};double col=.5;for(uint64_t i=0;i<n+30;i++){auto &t=xs[pick(rng)];Point q{t.a*p.x+t.b*p.y+t.e,t.c*p.x+t.d*p.y+t.f};p=variation(t.var,q);col=(col+t.color)*.5;if(i<30)continue;int px=int((p.x+1.5)/3*w),py=int((1.5-p.y)/3*h);if(px>=0&&px<w&&py>=0&&py<h){size_t k=size_t(py)*w+px;count[k]++;hue[k]+=col;}}uint32_t maxv=*std::max_element(count.begin(),count.end());std::ofstream out(argv[4],std::ios::binary);if(!out)throw std::runtime_error("Cannot open output");out<<"P6\n"<<w<<" "<<h<<"\n255\n";for(size_t i=0;i<count.size();i++){double v=maxv?log1p(count[i])/log1p(maxv):0;double c=count[i]?hue[i]/count[i]:0;unsigned char rgb[3]={static_cast<unsigned char>(255*v*c),static_cast<unsigned char>(255*v*(1-std::abs(c-.5))),static_cast<unsigned char>(255*v*(1-c))};out.write(reinterpret_cast<char*>(rgb),3);}if(!out)throw std::runtime_error("Write failed");std::cout<<"Rendered "<<w<<"x"<<h<<" samples="<<n<<"\n";}catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 1;}}
