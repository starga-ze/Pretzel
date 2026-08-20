#include "grpc/GrpcProtocol.h"

namespace pz::mgmtd
{

const char* grpcCmdToStr(GrpcCmd cmd) noexcept
{
    switch (cmd)
    {
    case GrpcCmd::Chat:          return "Chat";
    case GrpcCmd::CorpusStatus:  return "CorpusStatus";
    case GrpcCmd::CorpusRefresh: return "CorpusRefresh";
    case GrpcCmd::CorpusCancel:  return "CorpusCancel";
    case GrpcCmd::CorpusDocuments: return "CorpusDocuments";
    case GrpcCmd::Unknown:       break;
    }
    return "Unknown";
}

bool grpcCmdStreams(GrpcCmd cmd) noexcept
{
    return cmd == GrpcCmd::CorpusRefresh;
}

}
