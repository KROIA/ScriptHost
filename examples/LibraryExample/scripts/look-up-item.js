// @name Look up an item
// @description Asks for an item's name in a small dialog, then tells how many there are and where.

// One call builds the whole dialog. `name` puts the line edit's id into d.ids.
const d = ui.create({class: 'QDialog', title: 'Look up an item', layout: {type: 'vbox', items: [
    {class: 'QLabel', text: 'Which item?'},
    {class: 'QLineEdit', name: 'name', placeholderText: 'Screws'},
    // OK and Cancel close the dialog by themselves.
    {class: 'QDialogButtonBox', standardButtons: 'Ok|Cancel'}]}});

// The script waits here until the dialog closes; the window does not. The
// dialog itself goes when the run ends.
const answer = ui.exec(d.id);
if (!answer.accepted)
    return 'cancelled';
// d.ids.name, not 'name': a widget of the application with that name could
// come first.
const item = ui.property(d.ids.name, 'text').trim();
if (!item)
    return 'no item given';
try {
    const row = inventory.get(item);
    return row.item + ': ' + row.quantity + ', ' + row.location;
} catch (error) {
    return error.message;
}
