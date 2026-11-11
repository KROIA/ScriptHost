// @name Input logger
// @description Prints the keys pressed and the mouse clicks in the input area to the log. Space prints where the pointer is; Escape ends the script.

// Runs only after this script's end, between timers: the events wait until
// the script has returned. A loop or sleep() here would hold them back.
on('key', event => {
    if (event.type !== 'down' || event.repeat)
        return;
    if (event.key === 'Escape') {
        // No handler and no timer left: the run ends, with what the script returned.
        off('key');
        off('mouse');
        print('done');
        return;
    }
    if (event.key === 'Space') {
        const pointer = input.mouse();
        print('pointer at', pointer.x + ',' + pointer.y, pointer.inside ? '(inside)' : '(outside)');
        return;
    }
    const modifiers = event.modifiers.length ? event.modifiers.join('+') + '+' : '';
    print('key', modifiers + (event.key || event.text || event.code));
});

on('mouse', event => {
    if (event.type === 'down' || event.type === 'dblclick')
        print(event.type === 'down' ? 'click' : 'double click', event.button, 'at', event.x + ',' + event.y);
    else if (event.type === 'wheel')
        print('wheel', event.deltaY > 0 ? 'up' : 'down', 'at', event.x + ',' + event.y);
});

print('listening: click into the input area and type; Space for the pointer, Escape to end');
return 'listening';
