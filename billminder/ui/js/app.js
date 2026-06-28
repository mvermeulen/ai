const API_URL = 'http://localhost:8080/api';
let editingBillId = null;

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
        
        const activeBills = bills.filter(b => b.status !== 'paid');
        
        if (activeBills.length === 0) {
            tbody.innerHTML = '<tr><td colspan="6" style="text-align: center;">No active bills found.</td></tr>';
            return;
        }
        
        activeBills.forEach(bill => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td><strong>${bill.name}</strong></td>
                <td>$${bill.amount_expected.toFixed(2)}</td>
                <td>${bill.due_date}</td>
                <td style="text-transform: capitalize;">${bill.recurrence_rule}</td>
                <td><span class="status-badge status-${bill.status.toLowerCase()}">${bill.status}</span></td>
                <td>
                    ${bill.status !== 'paid' ? `<button class="btn small primary" onclick="payBill('${bill.id}', ${bill.amount_expected})">Mark Paid</button>` : ''}
                    <button class="btn small" onclick="editBill('${bill.id}')">Edit</button>
                    <button class="btn small" style="background-color: var(--danger); color: white;" onclick="deleteBill('${bill.id}')">Delete</button>
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
    
    const today = new Date().toISOString().split('T')[0];
    
    if (editingBillId) {
        try {
            const getResponse = await fetch(`${API_URL}/bills/${editingBillId}`);
            if (!getResponse.ok) {
                alert('Failed to fetch bill details for update');
                return;
            }
            const existingBill = await getResponse.json();
            
            existingBill.name = name;
            existingBill.amount_expected = amount;
            existingBill.due_date = dueDate;
            existingBill.recurrence_rule = recurrence;
            existingBill.updated_at = today;
            
            const response = await fetch(`${API_URL}/bills/${editingBillId}`, {
                method: 'PUT',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify(existingBill)
            });
            
            if (response.ok) {
                resetForm();
                loadBills();
            } else {
                alert('Failed to update bill');
            }
        } catch (error) {
            console.error('Error updating bill:', error);
            alert('Failed to update bill (network error)');
        }
    } else {
        const slug = name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/(^-|-$)/g, '');
        const id = slug ? `${slug}-${dueDate}` : `bill-${Date.now()}`;
        
        const bill = {
            id: id,
            name: name,
            amount_expected: amount,
            due_date: dueDate,
            recurrence_rule: recurrence,
            payee: '',
            status: 'upcoming',
            notes: '',
            group_id: slug,
            next_instance_id: '',
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
                resetForm();
                loadBills();
            } else {
                alert('Failed to add bill');
            }
        } catch (error) {
            console.error('Error adding bill:', error);
            alert('Failed to add bill (network error)');
        }
    }
}

async function editBill(id) {
    try {
        const response = await fetch(`${API_URL}/bills/${id}`);
        if (!response.ok) {
            alert('Failed to fetch bill details');
            return;
        }
        const bill = await response.json();
        
        document.getElementById('name').value = bill.name;
        document.getElementById('amount').value = bill.amount_expected;
        document.getElementById('due_date').value = bill.due_date;
        document.getElementById('recurrence').value = bill.recurrence_rule;
        
        editingBillId = id;
        
        document.querySelector('#bill-entry h2').textContent = 'Edit Bill';
        document.querySelector('#add-bill-form button[type="submit"]').textContent = 'Update Bill';
        
        if (!document.getElementById('cancel-edit-btn')) {
            const cancelBtn = document.createElement('button');
            cancelBtn.type = 'button';
            cancelBtn.id = 'cancel-edit-btn';
            cancelBtn.className = 'btn';
            cancelBtn.textContent = 'Cancel';
            cancelBtn.onclick = resetForm;
            cancelBtn.style.marginLeft = '10px';
            document.querySelector('#add-bill-form').appendChild(cancelBtn);
        }
        
        document.getElementById('bill-entry').scrollIntoView({ behavior: 'smooth' });
    } catch (error) {
        console.error('Error fetching bill:', error);
        alert('Failed to fetch bill (network error)');
    }
}

function resetForm() {
    document.getElementById('add-bill-form').reset();
    editingBillId = null;
    document.querySelector('#bill-entry h2').textContent = 'Add New Bill';
    document.querySelector('#add-bill-form button[type="submit"]').textContent = 'Save Bill';
    const cancelBtn = document.getElementById('cancel-edit-btn');
    if (cancelBtn) {
        cancelBtn.remove();
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

async function deleteBill(id) {
    if (!confirm('Are you sure you want to delete this bill instance?')) {
        return;
    }
    
    try {
        const response = await fetch(`${API_URL}/bills/${id}`, {
            method: 'DELETE'
        });
        
        if (response.ok) {
            loadBills();
        } else {
            alert('Failed to delete bill');
        }
    } catch (error) {
        console.error('Error deleting bill:', error);
        alert('Failed to delete bill (network error)');
    }
}
