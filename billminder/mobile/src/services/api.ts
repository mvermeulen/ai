import AsyncStorage from '@react-native-async-storage/async-storage';

const API_URL_KEY = '@billminder_api_url';

export const getApiUrl = async (): Promise<string> => {
  try {
    const url = await AsyncStorage.getItem(API_URL_KEY);
    // Return default if not found
    return url || 'http://192.168.68.62:8080/api';
  } catch (e) {
    return 'http://192.168.68.62:8080/api';
  }
};

export const setApiUrl = async (url: string) => {
  try {
    await AsyncStorage.setItem(API_URL_KEY, url);
  } catch (e) {
    console.error('Error saving API URL', e);
  }
};

export const fetchBills = async () => {
  const baseUrl = await getApiUrl();
  const response = await fetch(`${baseUrl}/bills`, {
    headers: { 'Cache-Control': 'no-cache', Pragma: 'no-cache' }
  });
  if (!response.ok) throw new Error('Failed to fetch bills');
  return response.json();
};

export const fetchInstances = async () => {
  const baseUrl = await getApiUrl();
  const response = await fetch(`${baseUrl}/instances`, {
    headers: { 'Cache-Control': 'no-cache', Pragma: 'no-cache' }
  });
  if (!response.ok) throw new Error('Failed to fetch instances');
  return response.json();
};

export const fetchBillDetails = async (id: string) => {
  const baseUrl = await getApiUrl();
  const response = await fetch(`${baseUrl}/bills/${id}`, {
    headers: { 'Cache-Control': 'no-cache', Pragma: 'no-cache' }
  });
  if (!response.ok) throw new Error('Failed to fetch bill details');
  return response.json();
};

export const fetchBillInstances = async (id: string) => {
  const baseUrl = await getApiUrl();
  const response = await fetch(`${baseUrl}/bills/${id}/instances`, {
    headers: { 'Cache-Control': 'no-cache', Pragma: 'no-cache' }
  });
  if (!response.ok) throw new Error('Failed to fetch instances for bill');
  return response.json();
};

export const addBill = async (bill: any) => {
  const baseUrl = await getApiUrl();
  const response = await fetch(`${baseUrl}/bills`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'Cache-Control': 'no-cache',
      Pragma: 'no-cache'
    },
    body: JSON.stringify(bill)
  });
  if (!response.ok) throw new Error('Failed to add bill');
  return response;
};

export const payBill = async (id: string, amountExpected: number) => {
  const baseUrl = await getApiUrl();
  const today = new Date().toISOString().split('T')[0];
  const response = await fetch(`${baseUrl}/instances/${id}/pay`, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      'Cache-Control': 'no-cache',
      Pragma: 'no-cache'
    },
    body: JSON.stringify({
      amount_paid: amountExpected,
      payment_date: today,
      notes: 'Paid via App'
    })
  });
  if (!response.ok) throw new Error('Failed to pay bill');
  return response;
};

export const deleteBillTemplate = async (id: string) => {
  const baseUrl = await getApiUrl();
  const response = await fetch(`${baseUrl}/bills/${id}`, {
    method: 'DELETE',
    headers: { 'Cache-Control': 'no-cache', Pragma: 'no-cache' }
  });
  if (!response.ok) throw new Error('Failed to delete bill');
  return response;
};

export const updateBillMetadata = async (id: string, billData: any) => {
  const baseUrl = await getApiUrl();
  const response = await fetch(`${baseUrl}/bills/${id}`, {
    method: 'PUT',
    headers: {
      'Content-Type': 'application/json',
      'Cache-Control': 'no-cache',
      Pragma: 'no-cache'
    },
    body: JSON.stringify(billData)
  });
  if (!response.ok) throw new Error('Failed to update metadata');
  return response;
};

export const deleteInstance = async (id: string) => {
  const baseUrl = await getApiUrl();
  const response = await fetch(`${baseUrl}/instances/${id}`, {
    method: 'DELETE',
    headers: { 'Cache-Control': 'no-cache', Pragma: 'no-cache' }
  });
  if (!response.ok) throw new Error('Failed to delete instance');
  return response;
};
