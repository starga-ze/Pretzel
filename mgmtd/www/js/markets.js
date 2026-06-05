export function renderMarkets(markets) {
  const container = document.getElementById("markets");
  container.innerHTML = "<h2>Market Overview</h2>";

  markets.forEach(market => {
    const card = document.createElement("div");
    card.className = "market-card";

    card.innerHTML = `
      <div class="market-header">
        <div class="market-title">Market ${market.marketId}</div>
        <div class="market-count">${market.itemCount} Items</div>
      </div>
      <table class="table market-table">
        <thead>
          <tr>
            <th>Item ID</th>
            <th>Name</th>
            <th>Price</th>
            <th>Qty</th>
            <th>Seller</th>
            <th>Created</th>
          </tr>
        </thead>
        <tbody>
        </tbody>
      </table>
    `;

    const tbody = card.querySelector("tbody");

    market.items.forEach(item => {
      const row = document.createElement("tr");
      row.innerHTML = `
        <td>${item.item_id}</td>
        <td>${item.name}</td>
        <td>${item.price}</td>
        <td>${item.quantity}</td>
        <td>${item.seller_session_id}</td>
        <td>${item.created_at_sec}</td>
      `;
      tbody.appendChild(row);
    });

    container.appendChild(card);
  });
}
