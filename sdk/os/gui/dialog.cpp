#include <sdk/os/gui.hpp>
#include <sdk/os/mem.hpp>

/**
 * Creates a dialog.
 * 
 * @param height The height of the dialog.
 * @param alignment The screen position to align the dialog with.
 * @param[in] title The string to display in the title bar of the dialog.
 * @param keyboard The keyboard to display when the dialog is shown.
 */
GUIDialog::GUIDialog(
	enum Height height, enum Alignment alignment,
	const char* title,
	enum KeyboardState keyboard
) : m_oldVTable(nullptr), m_vtable({}) {
	m_wrapped = GUIDialog_ctor(
		0,
		height, alignment,
		title,
		0, 0,
		keyboard
	);

	if (!m_wrapped)
		return;
	m_oldVTable = GetWrapped<GUIDialog_Wrapped>()->vtable;
	if (!m_oldVTable)
		return;
	memcpy(&m_vtable, m_oldVTable, sizeof(m_vtable));
	m_vtable.me = this;
	m_vtable.OnEvent.func = reinterpret_cast<decltype(m_vtable.OnEvent.func)>(OnEvent_Wrap);
}


GUIDialog::~GUIDialog() {
	RestoreVTable();
}

void GUIDialog::InstallVTable() {
	if (!m_wrapped || !m_oldVTable)
		return;
	GUIDialog_Wrapped *native = GetWrapped<GUIDialog_Wrapped>();
	if (native->vtable == m_oldVTable)
		native->vtable = &m_vtable;
}

void GUIDialog::RestoreVTable() {
	if (!m_wrapped || !m_oldVTable)
		return;
	GUIDialog_Wrapped *native = GetWrapped<GUIDialog_Wrapped>();
	if (native->vtable == &m_vtable)
		native->vtable = m_oldVTable;
}

int GUIDialog::OnEvent_Wrap(struct GUIDialog_Wrapped *dialog, struct GUIDialog_OnEvent_Data *event) {
	// Call the OnEvent function of our class (not of CASIO's one!)
	if (!dialog || !dialog->vtable || !dialog->vtable->me)
		return 0;
	return dialog->vtable->me->OnEvent(dialog, event);
}

// TODO: remove the dialog param and update apps
int GUIDialog::OnEvent(struct GUIDialog_Wrapped *dialog, struct GUIDialog_OnEvent_Data *event) {
	// We've overidden the entry for OnEvent in our vtable with one that
	// redirects to this method. Since we've done that, if we want to call the
	// actual base OnEvent method, we use the old vtable we backed up in the
	// constructor.
	(void)dialog;
	if (!m_oldVTable || !m_oldVTable->OnEvent.func)
		return 0;
	return m_oldVTable->OnEvent(m_wrapped, event);
}

/**
 * Returns the X position of the left edge of the dialog body, in pixels.
 *
 * @return The X position of the left edge of the dialog body, in pixels.
 */
uint16_t GUIDialog::GetLeftX() {
	return m_wrapped ? GetWrapped<GUIDialog_Wrapped>()->leftX : 0;
}

/**
 * Returns the Y position of the top edge of the dialog body, in pixels.
 *
 * @return The Y position of the top edge of the dialog body, in pixels.
 */
uint16_t GUIDialog::GetTopY() {
	return m_wrapped ? GetWrapped<GUIDialog_Wrapped>()->topY : 0;
}

/**
 * Returns the X position of the right edge of the dialog body, in pixels.
 *
 * @return The X position of the right edge of the dialog body, in pixels.
 */
uint16_t GUIDialog::GetRightX() {
	return m_wrapped ? GetWrapped<GUIDialog_Wrapped>()->rightX : 0;
}

/**
 * Returns the Y position of the bottom edge of the dialog body, in pixels.
 *
 * @return The Y position of the bottom edge of the dialog body, in pixels.
 */
uint16_t GUIDialog::GetBottomY() {
	return m_wrapped ? GetWrapped<GUIDialog_Wrapped>()->bottomY : 0;
}

/**
 * Adds a GUI element to the dialog.
 * 
 * @param element The GUI element to add.
 */
void GUIDialog::AddElement(GUIElement &element) {
	if (!m_wrapped || !m_vtable.AddElement.func)
		return;
	m_vtable.AddElement(m_wrapped, element.GetWrapped<void>(), 1);
}

/**
 * Refreshes the dialog, redrawing all components.
 */
void GUIDialog::Refresh() {
	if (!m_wrapped || !m_vtable.Refresh.func)
		return;
	m_vtable.Refresh(m_wrapped);
}

/**
 * Presents the dialog to the user. Blocks until the dialog is closed.
 * 
 * @return The result of the dialog.
 */
GUIDialog::DialogResult GUIDialog::ShowDialog() {
	if (!m_wrapped || !m_vtable.ShowDialog.func)
		return DialogResultCancel;
	InstallVTable();
	DialogResult result = static_cast<DialogResult>(m_vtable.ShowDialog(m_wrapped));
	RestoreVTable();
	return result;
}
