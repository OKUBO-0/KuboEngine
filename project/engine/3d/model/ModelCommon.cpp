#include "ModelCommon.h"

namespace Engine::Graphics3D {

void ModelCommon::Initialize(Engine::Base::DirectXCommon* dxCommon, Engine::Base::SrvManager* srvManager)
{
	dxCommon_ = dxCommon;
	srvManager_ = srvManager;

}

}
