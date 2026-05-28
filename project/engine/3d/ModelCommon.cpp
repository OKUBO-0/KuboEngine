#include "ModelCommon.h"

namespace Engine::Graphics3D {

void ModelCommon::Initialize(Engine::Base::DirectXCommon* dxCommon, Engine::Base::SrvManager* srvMnager)
{
	dxCommon_ = dxCommon;
	srvMnager_ = srvMnager;

}

}
