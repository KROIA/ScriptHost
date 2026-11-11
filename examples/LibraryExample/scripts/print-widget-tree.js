// @name Print the widget tree
// @description Logs every widget of the window that scripts can see, one line each, indented by depth: class, object name, id and text.

// One call returns the whole tree; walking it calls nothing more.
let count = 0;
(function show(node, depth) {
    const name = node.name ? ' "' + node.name + '"' : '';
    // `text` is the widget's text, title or contents: the script log's is long.
    const text = node.text ? ' - ' + node.text.replace(/\s+/g, ' ').slice(0, 40) : '';
    log('  '.repeat(depth) + node.class + name + ' #' + node.id + text);
    ++count;
    (node.children || []).forEach(child => show(child, depth + 1));
})(ui.tree(), 0);
// The ids work in every ui call of this session: ui.info(7), ui.hide(7).
return count + ' widgets';
