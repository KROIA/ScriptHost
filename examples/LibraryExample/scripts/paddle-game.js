// @name Paddle game
// @description Keep the ball up with the paddle and hit the star, on the game field. Click the field, then Left / Right move the paddle; Escape ends.

// Where the field is on the canvas (the canvas covers the window). Asked
// once - and again on a resize - as widgetRect() waits for the GUI thread.
const field = gfx.widgetRect('gameField');
if (!field)
    return 'there is no game field';
on('resize', () => Object.assign(field, gfx.widgetRect('gameField') || field));

// Styles made once, not per frame.
const paddleStyle = {fill: '#2050a0', stroke: 'none'};
const starStyle = {fill: '#f0c020', stroke: '#a07000', width: 1};
const textStyle = {fill: '#303030', font: {size: 14, bold: true}};
const hitStyle = {fill: '#e03030', font: {size: 14, bold: true}};
const border = {stroke: '#7090c0', width: 2};

const paddle = {x: field.width / 2 - 40, y: field.height - 18, w: 80, h: 8, speed: 0};
const ball = {x: 0, y: 0, r: 6, vx: 0, vy: 0};          // speeds in pixels per ms
const star = {x: field.width / 3, y: 40, r: 14};
let score = 0;
let best = 0;
const flash = gfx.tween(1, 0, 600, 'outQuad');          // the score turns red after a hit
flash.step(600);

function serve() {
    ball.x = field.width / 2;
    ball.y = field.height / 3;
    ball.vx = Math.random() < 0.5 ? -0.16 : 0.16;
    ball.vy = 0.2;
}
serve();

// Keys of the game field (forwardInput): held keys move the paddle.
on('key', event => {
    if (event.key === 'Left' || event.key === 'Right') {
        const direction = event.key === 'Left' ? -1 : 1;
        if (event.type === 'down')
            paddle.speed = direction * 0.45;
        else if (Math.sign(paddle.speed) === direction)
            paddle.speed = 0;
    } else if (event.key === 'Escape' && event.type === 'down') {
        // Nothing left that keeps the run: it ends, and its drawing goes.
        gfx.offFrame();
        off('key');
        off('resize');
        print('game over, best', best);
    }
});

// Once per screen refresh; dt in milliseconds. A slow frame is skipped, not
// queued, so the movement uses dt rather than counting frames.
gfx.onFrame(dt => {
    dt = Math.min(dt, 50);                              // no jump after a pause
    paddle.x = Math.max(0, Math.min(field.width - paddle.w, paddle.x + paddle.speed * dt));
    paddle.y = field.height - 18;
    ball.x += ball.vx * dt;
    ball.y += ball.vy * dt;
    if (ball.x < ball.r || ball.x > field.width - ball.r) {
        ball.vx = -ball.vx;
        ball.x = Math.max(ball.r, Math.min(field.width - ball.r, ball.x));
    }
    if (ball.y < ball.r)
        ball.vy = Math.abs(ball.vy);
    if (ball.vy > 0 && gfx.rectsOverlap(ball.x - ball.r, ball.y - ball.r, 2 * ball.r, 2 * ball.r,
                                        paddle.x, paddle.y, paddle.w, paddle.h)) {
        ball.vy = -Math.abs(ball.vy) * 1.04;            // a little faster each time
        ball.vx += (ball.x - (paddle.x + paddle.w / 2)) / paddle.w * 0.15;
        score += 1;
        flash.reset();
    }
    if (gfx.circlesOverlap(ball.x, ball.y, ball.r, star.x, star.y, star.r)) {
        score += 5;
        flash.reset();
        star.x = 30 + Math.random() * (field.width - 60);
        star.y = 30 + Math.random() * (field.height / 2);
    }
    if (ball.y > field.height + ball.r) {               // missed
        best = Math.max(best, score);
        score = 0;
        serve();
    }
    flash.step(dt);

    gfx.clear();
    gfx.rect(gfx.widget('gameField'), border);         // on the widget: no transform
    gfx.save();
    gfx.translate(field.x, field.y);                    // from here on: field coordinates
    gfx.save();
    gfx.style(starStyle);
    gfx.draw.star(star.x, star.y, star.r);              // the application's drawing function
    gfx.restore();                                      // the style as it was
    gfx.rect(paddle.x, paddle.y, paddle.w, paddle.h, paddleStyle);
    gfx.image('ball', ball.x - ball.r, ball.y - ball.r);
    gfx.text(8, 6, 'Score ' + score + '   best ' + best, flash.done ? textStyle : hitStyle);
    gfx.restore();
    gfx.present();
});

print('click the game field; Left / Right move the paddle, Escape ends');
return 'playing';
