// @name Restock low items
// @description Adds `amount` to the quantity of every item that has less than `threshold`.
// @param threshold number Items below this quantity are restocked (default 50)
// @param amount number How much to add (default 100)

const threshold = args.threshold === undefined ? 50 : args.threshold;
const amount = args.amount === undefined ? 100 : args.amount;
const restocked = [];
const items = inventory.list();
for (let i = 0; i < items.length; i++) {
    const entry = items[i];
    progress(i + 1, items.length, entry.item);
    if (entry.quantity < threshold) {
        inventory.setQuantity(entry.item, entry.quantity + amount);
        restocked.push(entry.item);
        log('restocked ' + entry.item);
    }
}
return {restocked: restocked, checked: items.length};
