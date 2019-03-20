#include "PvdCallback.hpp"

PvdCallback::PvdCallback(PxPhysics* mPhysics) : mPhysics(mPhysics)
{
}

PvdCallback::~PvdCallback()
{
	
}

void PvdCallback::onPvdConnected(PVD::PvdConnection&)
{
	//setup joint visualization.  This gets piped to pvd.
	mPhysics->getVisualDebugger()->setVisualizeConstraints(true);
	mPhysics->getVisualDebugger()->setVisualDebuggerFlag(physx::PxVisualDebuggerFlag::eTRANSMIT_CONTACTS, true);
	mPhysics->getVisualDebugger()->setVisualDebuggerFlag(physx::PxVisualDebuggerFlag::eTRANSMIT_SCENEQUERIES, true);
}

void PvdCallback::onPvdDisconnected(PVD::PvdConnection& conn)
{
	conn.release();
}
