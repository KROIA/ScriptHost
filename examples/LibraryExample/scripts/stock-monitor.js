// @name Stock monitor
// @description Checks the inventory every `seconds` and prints the items below `threshold` whenever that list changes. Runs until it is stopped (Scripts window, Runs tab, Stop).
// @param threshold number Items below this quantity are reported (default 50)
// @param seconds number How often to check (default 5)

const threshold = args.threshold === undefined ? 50 : args.threshold;
const seconds = args.seconds === undefined ? 5 : args.seconds;
let last = null;

function checkStock() {
    const low = inventory.list()
        .filter(entry => entry.quantity < threshold)
        .map(entry => entry.item + ' (' + entry.quantity + ')');
    const now = low.length === 0 ? 'every item has ' + threshold + ' or more' : 'low: ' + low.join(', ');
    if (now !== last)
        print(now);
    last = now;
}

checkStock();
// The timer keeps the run alive after the end of this script: it checks on
// until the run is stopped, or reaches its time limit.
setInterval(checkStock, seconds * 1000);
return {threshold: threshold, seconds: seconds};
