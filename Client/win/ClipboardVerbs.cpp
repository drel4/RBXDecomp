#include "ClipboardVerbs.h"

#include "v8datamodel/Selection.h"
#include "v8datamodel/Workspace.h"
#include "v8xml/SerializerV2.h"
#include "v8xml/XmlSerializer.h"

#include <boost/bind.hpp>
#include <cstring>
#include <sstream>

template <class T>
static T* createService(RBX::DataModel* dataModel)
{
	return dataModel ? dataModel->create<T>() : NULL;
}

static UINT clipboardFormat()
{
	static UINT format = RegisterClipboardFormat("Roblox");

	return format;
}

// Keep clipboard ownership exception-safe, including parser/allocation failures.
class ClipboardSession
{
public:
	ClipboardSession() : opened(AfxGetMainWnd() && AfxGetMainWnd()->OpenClipboard()) {}
	~ClipboardSession()
	{
		if (opened) {
			CloseClipboard();
		}
	}
	bool opened;

private:
	ClipboardSession(const ClipboardSession&);
	ClipboardSession& operator=(const ClipboardSession&);
};
static bool copySelectionToClipboard(RBX::DataModel* dataModel);

// STUB: ROBLOX 0x0040ffc0
void CutVerb::doIt(RBX::IDataState* dataState)
{
	if (!copySelectionToClipboard(dataModel)) {
		return;
	}
	RBX::DeleteBase::doIt(dataState);
}

// FUNCTION: ROBLOX 0x00410000
PasteVerb::PasteVerb(RBX::DataModel* dataModel, bool pasteInto)
	: RBX::Verb(dataModel, pasteInto ? "PasteInto" : "Paste"), m_unk0x0c(dataModel), m_unk0x10(pasteInto)
{
}

// FUNCTION: ROBLOX 0x00410060
CopyVerb::CopyVerb(RBX::DataModel* dataModel) : RBX::EditSelectionVerb("Copy", dataModel)
{
}

// STUB: ROBLOX 0x00412120
bool PasteVerb::isEnabled() const
{
	if (!m_unk0x0c || IsClipboardFormatAvailable(clipboardFormat()) != TRUE) {
		return false;
	}

	if (!m_unk0x10) {
		return true;
	}

	return (m_unk0x0c ? m_unk0x0c->create<RBX::Selection>() : NULL)->size() == 1;
}

// STUB: ROBLOX 0x004121a0
void PasteVerb::doIt(RBX::IDataState* dataState)
{
	if (!m_unk0x0c || !dataState || m_unk0x0c->findFirstChildByName("NetworkClient")) {
		return;
	}
	std::string text;
	{
		ClipboardSession clipboard;
		if (!clipboard.opened) {
			return;
		}
		HGLOBAL handle = GetClipboardData(clipboardFormat());
		SIZE_T size = handle ? GlobalSize(handle) : 0;
		if (!size) {
			return;
		}
		const char* buffer = static_cast<const char*>(GlobalLock(handle));
		if (!buffer) {
			return;
		}
		const char* end = static_cast<const char*>(std::memchr(buffer, 0, size));
		try {
			if (end) {
				text.assign(buffer, end);
			}
		}
		catch (...) {
			GlobalUnlock(handle);
			throw;
		}
		GlobalUnlock(handle);
		if (!end || text.empty()) {
			return;
		}
	}
	// Release the system clipboard before parsing or notifying the editor.
	std::istringstream stream(text);
	TextXmlParser machine(stream.rdbuf());
	std::auto_ptr<XmlElement> root(machine.parse());
	if (!root.get()) {
		return;
	}
	RBX::Selection* selection = createService<RBX::Selection>(m_unk0x0c);
	// Pin the Paste Into destination across selection/change callbacks.
	boost::shared_ptr<RBX::Instance> parent = selection->front();
	if (!m_unk0x10) {
		parent = RBX::shared_from(m_unk0x0c->getWorkspace());
	}
	if (!parent) {
		return;
	}
	std::vector<boost::shared_ptr<RBX::Instance> > instances;
	SerializerV2 serializer;
	serializer.loadInstances(root.get(), instances);
	if (instances.empty()) {
		return;
	}
	RBX::InsertMode insertMode = m_unk0x10 ? RBX::INSERT_TO_TREE : RBX::INSERT_TO_3D_VIEW;
	m_unk0x0c->getWorkspace()->insertInstances(instances, parent.get(), insertMode, RBX::SUPPRESS_PROMPTS);
	selection->setSelection(instances.begin(), instances.end());

	RBX::XmlState* undoState = new RBX::UndoState();
	RBX::XmlState* redoState = new RBX::RedoState();
	for (std::vector<boost::shared_ptr<RBX::Instance> >::iterator iter = instances.begin(); iter != instances.end();
		 ++iter) {
		undoState->addDelete(*iter->get());
		redoState->addParentProperty(*iter->get());
	}

	dataState->pushState(getName(), undoState, redoState);
	dataState->setDirty(true);
}

static bool copySelectionToClipboard(RBX::DataModel* dataModel)
{
	if (!dataModel) {
		return false;
	}
	RBX::Selection* selection = createService<RBX::Selection>(dataModel);
	boost::shared_ptr<const std::vector<boost::shared_ptr<RBX::Instance> > > items = selection->getSelection2();
	if (items->empty()) {
		return false;
	}
	std::auto_ptr<XmlElement> root(SerializerV2::newRootElement());
	std::for_each(items->begin(), items->end(), boost::bind(&RBX::FUN_00597490, root.get(), _1));
	std::ostringstream stream;
	TextXmlWriter writer(stream);
	writer.serialize(root.get());
	std::string text = stream.str();
	ClipboardSession clipboard;
	if (!clipboard.opened) {
		return false;
	}
	HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, text.length() + 1);
	if (!handle) {
		return false;
	}
	char* buffer = static_cast<char*>(GlobalLock(handle));
	if (!buffer) {
		GlobalFree(handle);
		return false;
	}
	std::memcpy(buffer, text.c_str(), text.length() + 1);
	GlobalUnlock(handle);
	if (!EmptyClipboard() || !SetClipboardData(clipboardFormat(), handle)) {
		GlobalFree(handle);
		return false;
	}
	// Windows owns the allocation only after SetClipboardData succeeds.
	return true;
}

// STUB: ROBLOX 0x004125e0
void CopyVerb::doIt(RBX::IDataState* dataState)
{
	copySelectionToClipboard(dataModel);
}
