const API_URL = 'http://localhost:8080/api';
let currentDetailBillId = null;

document.addEventListener('DOMContentLoaded', () => {
    loadBills();
    
    document.getElementById('add-bill-form').addEventListener('submit', async (e) => {
        e.preventDefault();
        await addBill();
    });

    const editMetaForm = document.getElementById('edit-bill-metadata-form');
    if (editMetaForm) {
        editMetaForm.addEventListener('submit', async (e) => {
            e.preventDefault();
            await updateBillMetadata();
        });
    }

    const editInstForm = document.getElementById('edit-instance-form');
    if (editInstForm) {
        editInstForm.addEventListener('submit', async (e) => {
            e.preventDefault();
            await saveInstanceEdit();
        });
    }
});

async function loadBills() {
    try {
        const [billsRes, instancesRes] = await Promise.all([
            fetch(`${API_URL}/bills`),
            fetch(`${API_URL}/instances`)
        ]);
        
        if (!billsRes.ok || !instancesRes.ok) throw new Error("Failed to fetch data");
        
        const bills = await billsRes.json();
        const instances = await instancesRes.json();
        
        const tbody = document.getElementById('bills-tbody');
        tbody.innerHTML = '';
        
        if (bills.length === 0) {
            tbody.innerHTML = '<tr><td colspan="6" style="text-align: center;">No active bills found.</td></tr>';
            return;
        }
        
        const mappedBills = bills.map(bill => {
            const activeInstance = instances.find(inst => inst.bill_id === bill.id && inst.status !== 'paid');
            return {
                ...bill,
                activeInstance: activeInstance
            };
        });

        mappedBills.sort((a, b) => {
            const dateA = a.activeInstance ? a.activeInstance.due_date : '9999-12-31';
            const dateB = b.activeInstance ? b.activeInstance.due_date : '9999-12-31';
            return dateA.localeCompare(dateB);
        });
        
        mappedBills.forEach(bill => {
            const activeInstance = bill.activeInstance;
            
            const tr = document.createElement('tr');
            
            let amount = bill.default_amount;
            let dueDate = '-';
            let status = 'No Active';
            let markPaidBtn = '';
            
            if (activeInstance) {
                amount = activeInstance.amount_expected;
                dueDate = activeInstance.due_date;
                status = activeInstance.status;
                markPaidBtn = `<button class="btn small primary" onclick="payBill('${activeInstance.id}', ${amount})">Mark Paid</button>`;
            }

            tr.innerHTML = `
                <td><strong><a href="#" onclick="viewBillDetails('${bill.id}'); return false;">${bill.name}</a></strong></td>
                <td>$${amount.toFixed(2)}</td>
                <td>${dueDate}</td>
                <td style="text-transform: capitalize;">${bill.recurrence_rule}</td>
                <td><span class="status-badge status-${status.toLowerCase().replace(' ', '-')}">${status}</span></td>
                <td>
                    ${markPaidBtn}
                    <button class="btn small" style="background-color: var(--danger); color: white;" onclick="deleteBillTemplate('${bill.id}')">Delete Bill</button>
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
    try {
        const response = await fetch(`${API_URL}/instances/${id}/pay`, {
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
            if (currentDetailBillId) {
                loadBillInstances(currentDetailBillId);
            } else {
                loadBills();
            }
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
        const response = await fetch(`${API_URL}/instances/${id}`, {
            method: 'DELETE'
        });
        
        if (response.ok) {
            if (currentDetailBillId) {
                loadBillInstances(currentDetailBillId);
            } else {
                loadBills();
            }
        } else {
            alert('Failed to delete bill');
        }
    } catch (error) {
        console.error('Error deleting bill:', error);
        alert('Failed to delete bill (network error)');
    }
}

async function deleteBillTemplate(id) {
    if (!confirm('Are you sure you want to delete this bill and all its instances?')) {
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

async function viewBillDetails(billId) {
    currentDetailBillId = billId;
    
    const dashboardView = document.getElementById('view-dashboard');
    if (!dashboardView) {
        alert('Your browser has cached the old HTML file! Please perform a hard refresh (Ctrl+Shift+R or Cmd+Shift+R) to load the new design.');
        // Fallback for old HTML
        document.getElementById('bill-entry').style.display = 'none';
        document.getElementById('bill-list').style.display = 'none';
        document.getElementById('bill-detail-view').style.display = 'block';
    } else {
        dashboardView.classList.remove('active');
        setTimeout(() => {
            document.getElementById('view-detail').classList.add('active');
        }, 50);
    }

    try {
        const response = await fetch(`${API_URL}/bills/${billId}`);
        if (!response.ok) throw new Error("Failed to fetch bill details");
        const bill = await response.json();

        document.getElementById('detail-bill-name').textContent = `${bill.name} Details`;
        document.getElementById('detail-url').value = bill.url || '';
        document.getElementById('detail-account').value = bill.account || '';
        document.getElementById('detail-password').value = bill.password || '';
        
        loadBillInstances(billId);
    } catch(e) {
        console.error(e);
        alert('Failed to fetch bill details');
    }
}

async function loadBillInstances(billId) {
    try {
        const response = await fetch(`${API_URL}/bills/${billId}/instances`);
        if (!response.ok) throw new Error("Failed to fetch instances");
        const instances = await response.json();

        const tbody = document.getElementById('detail-instances-tbody');
        tbody.innerHTML = '';
        if (instances.length === 0) {
            tbody.innerHTML = '<tr><td colspan="4" style="text-align: center;">No instances found.</td></tr>';
            return;
        }

        instances.forEach(inst => {
            const tr = document.createElement('tr');
            tr.innerHTML = `
                <td>$${inst.amount_expected.toFixed(2)}</td>
                <td>${inst.due_date}</td>
                <td><span class="status-badge status-${inst.status.toLowerCase()}">${inst.status}</span></td>
                <td>
                    ${inst.status !== 'paid' ? `<button class="btn small primary" onclick="payBill('${inst.id}', ${inst.amount_expected})">Mark Paid</button>` : ''}
                    <button class="btn small outline" onclick="openEditModal('${inst.id}', ${inst.amount_expected}, '${inst.due_date}')">Edit</button>
                    <button class="btn small" style="background-color: var(--danger); color: white;" onclick="deleteBill('${inst.id}')">Delete</button>
                </td>
            `;
            tbody.appendChild(tr);
        });
    } catch(e) {
        console.error(e);
        document.getElementById('detail-instances-tbody').innerHTML = '<tr><td colspan="4" style="color: red;">Error loading instances.</td></tr>';
    }
}

async function updateBillMetadata() {
    if (!currentDetailBillId) return;
    try {
        const response = await fetch(`${API_URL}/bills/${currentDetailBillId}`);
        if (!response.ok) throw new Error("Failed to fetch bill details");
        const bill = await response.json();

        bill.url = document.getElementById('detail-url').value;
        bill.account = document.getElementById('detail-account').value;
        bill.password = document.getElementById('detail-password').value;

        const updateRes = await fetch(`${API_URL}/bills/${currentDetailBillId}`, {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(bill)
        });

        if (updateRes.ok) {
            alert('Metadata saved!');
        } else {
            alert('Failed to save metadata');
        }
    } catch(e) {
        console.error(e);
        alert('Failed to save metadata');
    }
}

function showDashboard() {
    const detailView = document.getElementById('view-detail');
    if (!detailView) {
        // Fallback for old HTML
        document.getElementById('bill-entry').style.display = 'block';
        document.getElementById('bill-list').style.display = 'block';
        document.getElementById('bill-detail-view').style.display = 'none';
    } else {
        detailView.classList.remove('active');
        setTimeout(() => {
            document.getElementById('view-dashboard').classList.add('active');
        }, 50);
    }
    
    currentDetailBillId = null;
    loadBills();
}

function openEditModal(id, amount, date) {
    document.getElementById('edit-inst-id').value = id;
    document.getElementById('edit-inst-amount').value = amount;
    document.getElementById('edit-inst-date').value = date;
    document.getElementById('edit-instance-modal').classList.add('active');
}

function closeEditModal() {
    document.getElementById('edit-instance-modal').classList.remove('active');
}

async function saveInstanceEdit() {
    const id = document.getElementById('edit-inst-id').value;
    const amount = parseFloat(document.getElementById('edit-inst-amount').value);
    const date = document.getElementById('edit-inst-date').value;

    try {
        const response = await fetch(`${API_URL}/instances/${id}`, {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                amount_expected: amount,
                due_date: date
            })
        });

        if (response.ok) {
            closeEditModal();
            if (currentDetailBillId) {
                loadBillInstances(currentDetailBillId);
            }
        } else {
            alert('Failed to save instance edit');
        }
    } catch(e) {
        console.error(e);
        alert('Error saving instance edit');
    }
}
