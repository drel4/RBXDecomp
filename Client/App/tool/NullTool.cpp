#include "tool/NullTool.h"

#include "humanoid/Humanoid.h"
#include "network/Players.h"
#include "util/Name.h"
#include "util/NavKeys.h"
#include "util/UserInputBase.h"
#include "v8datamodel/ModelInstance.h"
#include "v8datamodel/UserController.h"
#include "v8datamodel/Workspace.h"
#include "v8tree/Service.h"

namespace RBX {

// GLOBAL: ROBLOX 0x0094275c
char sNullTool[] = "NullTool";

char sNewNullTool[] = "NewNullTool";

// FUNCTION: WEBSERVICE 0x100c5000
// FUNCTION: ROBLOX 0x00622790
NewNullTool::~NewNullTool()
{
}

// FUNCTION: WEBSERVICE 0x100c50a0
// FUNCTION: ROBLOX 0x00622830
void NewNullTool::onMouseIdle(const UIEvent& uiEvent)
{
	if (getPartByLocalCharacter(uiEvent, waypoint) && characterCanReach(waypoint)) {
		cursor = "ArrowCursor";
	}
	else {
		cursor = "ArrowFarCursor";
	}
}

// FUNCTION: WEBSERVICE 0x100c50f0
// FUNCTION: ROBLOX 0x00622880
void NewNullTool::onMouseHover(const UIEvent& uiEvent)
{
	if (getPartByLocalCharacter(uiEvent, waypoint) && characterCanReach(waypoint)) {
		cursor = "ArrowCursor";
		hasWaypoint = true;
	}
	else {
		cursor = "ArrowFarCursor";
		hasWaypoint = false;
	}
}

// STUB: WEBSERVICE 0x100c5140
// STUB: ROBLOX 0x006228d0
MouseCommand* NewNullTool::onMouseDown(const UIEvent& uiEvent)
{
	G3D::Vector3 hitPoint;
	PartInstance* part = getPartByLocalCharacter(uiEvent, hitPoint);
	if (part && characterCanReach(hitPoint)) {
		if (ModelInstance* character = Network::Players::findLocalCharacter(workspace)) {
			if (!character->contains(part)) {
				if (Humanoid* humanoid = Humanoid::modelIsCharacter(character)) {
					humanoid->moveTo(hitPoint, part);
					// Clicking starts the walk without dismissing the hover preview.
					waypoint = hitPoint;
					hasWaypoint = true;
				}
			}
		}
	}

	return this;
}

// FUNCTION: ROBLOX 0x006229d0
NullTool::NullTool(Workspace* workspace) : Named<MouseCommand, sNullTool>(workspace)
{
}

// FUNCTION: WEBSERVICE 0x100c5240
// FUNCTION: ROBLOX 0x00622a20
NewNullTool::NewNullTool(Workspace* workspace)
	: Named<MouseCommand, sNewNullTool>(workspace), cursor("ArrowCursor"), hasWaypoint(false)
{
}

// STUB: WEBSERVICE 0x100c5360
// STUB: ROBLOX 0x00622bc0
void NewNullTool::render3dAdorn(Adorn* adorn)
{
	// Hover is independent of the clicked movement destination. Clicking or
	// reaching that destination must not dismiss the cursor's hover preview.
	if (!hasWaypoint) return;
	ControllerService* service = ServiceProvider::find<ControllerService>(workspace);
	if (service && service->getHardwareDevice()) {
		NavKeys navKeys;
		service->getHardwareDevice()->getNavKeys(navKeys);
		if (navKeys.navKeyDown()) return;
	}
	Humanoid::renderWaypoint(adorn, waypoint);
}

} // namespace RBX

template const RBX::Name& RBX::Name::doDeclare<RBX::sNewNullTool>();
template void RBX::Name::callDoDeclare<RBX::sNewNullTool>();
