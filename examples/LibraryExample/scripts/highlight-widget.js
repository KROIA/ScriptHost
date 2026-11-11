// @name Highlight a widget
// @description Draws a frame around a widget of the window, found by its object name, and an arrow to it - for five seconds.
// @param widget string The widget's object name: scriptInput, gameField, inventoryTable, scriptLog (default scriptInput)

const name = args.widget || 'scriptInput';
// widgetRect() asks the application's thread: fine once, not for every frame.
const now = gfx.widgetRect(name);
if (!now)
    return "there is no visible widget '" + name + "'";
// The arrow starts above the widget, or below it when there is no room.
const above = now.y > 80;
const x = now.x + now.width * 0.7;
const y = above ? now.y - 50 : now.y + now.height + 50;

// Drawn on the widget itself, not at numbers: the frame and the arrow's end
// follow it when the window is resized or the splitter moved.
const target = gfx.widget(name);
gfx.roundedRect(target, 6, {stroke: '#e03030', width: 3, padding: 4});
gfx.arrow(x, y, target, {stroke: '#e03030', width: 3, padding: 8});
gfx.text(x, y + (above ? -4 : 4), 'here', {fill: '#e03030', align: 'center', valign: above ? 'bottom' : 'top',
                                          font: {size: 16, bold: true}});
gfx.present();

// A drawing goes when its run ends: a timer keeps this one up for five seconds.
setTimeout(() => print('highlight over'), 5000);
return 'highlighting ' + name;
