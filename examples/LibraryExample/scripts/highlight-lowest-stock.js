// @name Highlight the lowest stock
// @description Marks the Quantity cell of the item with the lowest stock in the inventory table, until the run is stopped. The mark follows scrolling, column resizing and window moves, and moves to another row when the lowest item changes.

let shown = null;

function markLowest() {
    // The Quantity column (column 1) of every row, in view or not. Each cell
    // comes with its `ref`, {row, column}: the name of that cell.
    const cells = ui.items('inventoryTable', {all: true, columns: [1, 1]}).items;
    let lowest = null;
    for (const cell of cells) {
        if (!lowest || Number(cell.text) < Number(lowest.text))
            lowest = cell;
    }
    const key = lowest ? JSON.stringify(lowest.ref) : '';
    if (key === shown)
        return;
    shown = key;
    gfx.clear();
    if (lowest) {
        // An item anchor: the widget plus the cell's ref. The canvas works
        // out where the cell is whenever it paints, so the mark follows the
        // table without another present(). Out of view, it is not drawn; at
        // the edge of the view, it is cut there (clip, the default).
        const cell = Object.assign({widget: 'inventoryTable'}, lowest.ref);
        gfx.rect(cell, {stroke: '#e03030', width: 3, fill: '#30e03030'});
        print('lowest stock: row ' + lowest.ref.row + ', quantity ' + lowest.text);
    }
    gfx.present();
}

markLowest();
// The timer looks again every two seconds - another item may have become the
// lowest - and keeps the run, and with it the mark, alive until it is stopped.
setInterval(markLowest, 2000);
return 'marking the lowest stock';
