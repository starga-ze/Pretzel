export function renderSessions(sessions) {
  const tbody = document.getElementById("sessionTableBody");
  tbody.innerHTML = "";

  sessions.forEach(s => {
    const row = document.createElement("tr");
    row.innerHTML = `
      <td>${s.sid}</td>
      <td>${s.state}</td>
      <td>${s.tls_fd}</td>
      <td>${s.tcp_fd}</td>
      <td>${s.udp_fd}</td>
    `;
    tbody.appendChild(row);
  });
}
