// @name Rearrange the window
// @description Turns the window's panes side by side (or back on top of each other), all the same size, and puts everything back after a few seconds.
// @param seconds number How long the new arrangement stays (default 3)

const split = ui.find({class: 'QSplitter'})[0];
if (!split)
    return 'the window has no splitter to rearrange';
// One size per pane; the handles between the panes are widgets too. The
// sizes come first: ui.restore() undoes the latest change first, and sizes
// put back across a turned splitter would not fit.
const panes = ui.children(split.id).filter(child => child.class !== 'QSplitterHandle');
ui.setSplitterSizes(split.id, panes.map(() => 100));
const vertical = ui.property(split.id, 'orientation') === 'Vertical';
ui.setProperty(split.id, 'orientation', vertical ? 'Horizontal' : 'Vertical');

// ui.restore() puts back what this run changed with the ui tools, the latest
// first. Only while the run lives: the timer keeps it alive until then.
const seconds = args.seconds || 3;
setTimeout(() => print('restored ' + ui.restore().restored + ' changes'), seconds * 1000);
return 'rearranged for ' + seconds + ' s';
