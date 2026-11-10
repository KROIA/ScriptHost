// @name Inventory report
// @description Totals per location. Changes nothing.

const perLocation = {};
let total = 0;
for (const entry of inventory.list()) {
    perLocation[entry.location] = (perLocation[entry.location] || 0) + entry.quantity;
    total += entry.quantity;
}
log('locations: ' + Object.keys(perLocation).length);
return {total: total, perLocation: perLocation};
