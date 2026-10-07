#include "algorithm_process.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <iostream>
using namespace radar_qt_example;
void require(bool ok,const char* msg) { if(!ok) throw std::runtime_error(msg); }
template<class F> void rejects(F f) { bool caught=false;try{f();}catch(const std::exception&){caught=true;}require(caught,"Expected rejection"); }
CpiBuffer cpi_with(unsigned ranges=3) {
    CpiBuffer cpi;std::vector<uestcradar::ComplexFloat32> samples(ranges);
    uestcradar::PulseCompressionMetadata md{1,ranges,0,64,4.8828125};
    for(unsigned p=0;p<64;++p) {
        md.pulse_index=p;
        for(unsigned r=0;r<ranges;++r) samples[r]={float(p*10+r),-float(p*10+r)};
        cpi.push(md,samples);
    }
    return cpi;
}
void test_frames() {
    auto cpi=cpi_with();auto bytes=frame_converter::to_algorithm_frame(cpi);
    require(bytes.size()==20+64*3*8,"Input length");
    require(bytes.left(16).toHex()=="bc1caaffffffaaffff7fff7fff7fff7f","Magic bytes");
    require(bytes.mid(16,4).toHex()=="03004000","Dimension order/endian");
    QDataStream stream(bytes.mid(20));stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    for(int p=0;p<64;++p) for(int r=0;r<3;++r) {
        float i,q;stream>>i>>q;require(i==p*10+r && q==-(p*10+r),"Input I/Q layout");
    }
    auto rd=frame_converter::to_rd_frame({2,3,{1,2,3,4,5,6}},cpi);
    require(rd.values==std::vector<float>({1,4,2,5,3,6}),"Output transpose");
    require(rd.metadata.doppler_bin_count==2 && rd.metadata.range_bin_count==3,"Actual dimensions");
    rejects([&]{frame_converter::to_rd_frame({2,3,{1}},cpi);});
    rejects([&]{frame_converter::to_rd_frame({0,3,{}},cpi);});
    rejects([&]{frame_converter::to_rd_frame({1,1,{std::numeric_limits<double>::infinity()}},cpi);});
    rejects([&]{frame_converter::to_rd_frame({1,1,{1e100}},cpi);});
    rejects([&]{frame_converter::to_algorithm_frame(CpiBuffer{});});
    CpiBuffer broken;uestcradar::PulseCompressionMetadata md{1,1,0,64,1.};
    std::vector<uestcradar::ComplexFloat32> sample{{1,2}};
    broken.push(md,sample);md.pulse_index=2;broken.push(md,sample);
    for(unsigned p=3;p<64;++p){md.pulse_index=p;broken.push(md,sample);}
    require(!broken.ready(),"Incomplete CPI accepted");
    for(unsigned p=0;p<64;++p){md.pulse_index=p;broken.push(md,sample);}
    require(broken.ready(),"CPI recovery failed");
    md.range_bin_count=kMaxInputRangeBinCount+1;rejects([&]{broken.push(md,sample);});
}
void test_shared_memory() {
    const auto key="qt5_test_"+QString::number(QCoreApplication::applicationPid());
    InputChannel writer(key);QSharedMemory reader(key);require(reader.attach(),"Reader attach");
    QByteArray bytes(100003,'x');bytes[123]='a';require(writer.write(bytes),"Write");
    require(!writer.write(bytes),"Unread input overwritten");
    QByteArray actual;
    while(actual.size()<bytes.size()) {
        require(reader.lock(),"Reader lock");auto* h=static_cast<quint32*>(reader.data());
        int count=std::min<int>(32768,h[2]-h[3]);
        actual.append(static_cast<char*>(reader.data())+16+h[3],count);h[3]+=count;
        if(h[3]==h[2])h[2]=h[3]=0;
        reader.unlock();
    }
    require(actual==bytes,"Chunk order");require(writer.write(bytes),"Buffer reuse");
    rejects([&]{writer.write(QByteArray(kInputShmBytes,'x'));});
}
void test_process(const QString& exe) {
    QTemporaryDir dir;require(dir.isValid(),"Temp directory");
    for(const auto* csv:{"subband_filter_32x32.csv","subband_filter_64x64.csv"}) {
        QFile f(dir.path()+"/"+csv);require(f.open(QIODevice::WriteOnly),"CSV fixture");
    }
    for(const auto* mode:{"normal","exit","timeout"}) {
        qputenv("GFKD_TEST_MODE",mode);
        AlgorithmProcess process(exe,dir.path(), mode==std::string("normal")?10000:100);
        process.start();process.write("test CPI");
        if(mode==std::string("normal")) {
            auto r=process.wait_result();require(r.frequencies==2 && r.ranges==3,"Result shape");
            require(r.values==std::vector<double>({1,2,3,4,5,6}),"Output shared memory order");
        } else rejects([&]{process.wait_result();});
        process.stop();
    }
    qunsetenv("GFKD_TEST_MODE");
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {test_frames();test_shared_memory();test_process(argv[1]);
        std::cout<<"rd-algorithm-test: PASS\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
