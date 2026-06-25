const API_URL = 'http://localhost:8080/api';

document.addEventListener('DOMContentLoaded', () => {
    loadBills();
    
    document.getElementById('add-bill-form').addEventListener('submit', async (e) => {
        e.preventDefault();
        await addBill();
    });
});

async function loadBills() {
    try {
        const response = await fetch(`${API_URL}/bills`);
        const bills = await response.json();
        
        const tbody = document.getElementById('bills-tbody');
        tbody.innerHTML = '';
        
        if (bills.length === 0) {
            tbody.innerHTML = '<tr><td colspan="6" style="text-align: center;">No bills found.</td></tr>';
            return;
        }
        
        bills.forEach(bill => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td><strong>${bill.name}</strong></td>
                <td>$${bill.amount_expected.toFixed(2)}</td>
                <td>${bill.due_date}</td>
                <td style="text-transform: capitalize;">${bill.recurrence_rule}</td>
                <td><span class="status-badge status-${bill.status.toLowerCase()}">${bill.status}</span></td>
                <td>
                    ${bill.status !== 'paid' ? `<button class="btn small primary" onclick="payBill('${bill.id}', ${bill.amount_expected})">Mark Paid</button>` : ''}
                </td>
            `;
            tbody.appendChild(tr);
        });
    } catch (error) {
        console.error('Error loading bills:', error);
        document.getElementById('bills-tbody').innerHTML = '<tr><td colspan="6" style="color: var(--danger); text-align: center;">Error loading bills. Is the backend server running?</td></tr>';
    }
}

async function addBill() {
    const name = document.getElementById('name').value;
    const amount = parseFloat(document.getElementById('amount').value);
    const dueDate = document.getElementById('due_date').value;
    const recurrence = document.getElementById('recurrence').value;
    
    // Generate simple ID
    const id = 'bill-' + Date.now();
    const today = new Date().toISOString().split('T')[0];
    
    const bill = {
        id: id,
        name: name,
        amount_expected: amount,
        due_date: dueDate,
        recurrence_rule: recurrence,
        payee: '',
        status: 'upcoming',
        notes: '',
        created_at: today,
        updated_at: today
    };
    
    try {
        const response = await fetch(`${API_URL}/bills`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(bill)
        });
        
        if (response.ok) {
            document.getElementById('add-bill-form').reset();
            loadBills();
        } else {
            alert('Failed to add bill');
        }
    } catch (error) {
        console.error('Error adding bill:', error);
        alert('Failed to add bill (network error)');
    }
}

async function payBill(id, amountExpected) {
    const today = new Date().toISOString().split('T')[0];
    // For MVP, just assume they paid exactly what was expected.
    try {
        const response = await fetch(`${API_URL}/bills/${id}/pay`, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                amount_paid: amountExpected,
                payment_date: today,
                notes: 'Paid via Dashboard'
            })
        });
        
        if (response.ok) {
            loadBills();
        } else {
            alert('Failed to record payment');
        }
    } catch (error) {
        console.error('Error paying bill:', error);
        alert('Failed to pay bill (network error)');
    }
}
