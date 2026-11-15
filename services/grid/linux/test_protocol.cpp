#include "Streaming.h"
#include <G3D/Table.h>
#include <StringCompressor.h>
#include <cstdio>
#include <cstring>
#include <cmath>

int main() {
    static_assert(sizeof(int)==4,"Legacy protocol requires 32-bit integers");
    static_assert(sizeof(float)==4 && sizeof(double)==8,"Legacy IEEE float widths");
    RBX::Guid::Data first={&RBX::Name::declare("RBXScopeA",0),123};
    RBX::Guid::Data second={&RBX::Name::declare("RBXScopeZ",0),123};
    std::map<RBX::Guid::Data,int> identities;
    identities[first]=1; identities[second]=2;
    if(identities.size()!=2 || identities[first]!=1 || identities[second]!=2) return 8;
    RBX::Guid a,b; a.assign(first); b.assign(second);
    if(RBX::Guid::compare(&a,&b)!=-1 || RBX::Guid::compare(&b,&a)!=1) return 9;
    for(size_t alignment=8;alignment<=64;alignment*=2) {
        void* block=G3D::System::alignedMalloc(65537,alignment);
        if(!block || reinterpret_cast<size_t>(block)%alignment) return 7;
        std::memset(block,0x5a,65537);
        G3D::System::alignedFree(block);
    }
    int addresses[2];
    if(hashCode(static_cast<const void*>(&addresses[0]))==
       hashCode(static_cast<const void*>(&addresses[1]))) return 4;
    RakNet::BitStream marker;
    marker.Write((int)-123456789);
    marker.Write((unsigned char)0x5a);
    long id=0;
    RBX::Network::operator>>(marker,id);
    unsigned char sentinel=0;
    if(!marker.Read(sentinel) || id!=-123456789 || sentinel!=0x5a) return 1;

    RakNet::BitStream compressed,reference;
    compressed.WriteCompressed(0.25);
    reference.Write((unsigned int)2684354560u);
    if(compressed.GetNumberOfBitsUsed()!=32 ||
       std::memcmp(compressed.GetData(),reference.GetData(),4)) return 2;
    double value=0;
    if(!compressed.ReadCompressed(value) || std::fabs(value-0.25)>1e-9) return 3;
    StringCompressor::AddReference();
    const std::string source="legacy Windows string packet";
    RakNet::BitStream text,expected;
    RBX::Network::operator<<(text,source);
    expected.Write(static_cast<unsigned int>(source.size()));
    stringCompressor->EncodeString(source.c_str(),source.size()+1,&expected);
    if(text.GetNumberOfBitsUsed()!=expected.GetNumberOfBitsUsed() ||
       std::memcmp(text.GetData(),expected.GetData(),text.GetNumberOfBytesUsed())) return 5;
    std::string decoded;
    RBX::Network::operator>>(text,decoded);
    StringCompressor::RemoveReference();
    if(decoded!=source) return 6;
    std::printf("NATIVE_LEGACY_WIRE_WIDTHS_PASS pointer_bits=%zu long_bits=%zu\n",sizeof(void*)*8,sizeof(long)*8);
    return 0;
}
