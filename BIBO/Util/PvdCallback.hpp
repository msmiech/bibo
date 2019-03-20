#pragma once

#ifndef PX_INCLUDED
#define PX_INCLUDED
#include "PhysX/PxPhysicsAPI.h"
#include "PhysX/extensions/PxExtensionsAPI.h"
#endif

namespace physx
{
	class Picking;
	namespace debugger { namespace comm {} }
}


namespace PVD {
	using namespace physx::debugger;
	using namespace physx::debugger::comm;
}

using namespace physx;

class PvdCallback : public PVD::PvdConnectionHandler
{
	PxPhysics* mPhysics;
public:
	PvdCallback(PxPhysics* mPhysics);
	virtual ~PvdCallback();

	virtual			void									onPvdSendClassDescriptions(PVD::PvdConnection&) {}
	virtual			void									onPvdConnected(PVD::PvdConnection& inFactory);
	virtual			void									onPvdDisconnected(PVD::PvdConnection& inFactory);

};
