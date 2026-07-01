import React, { useState, useEffect, useCallback } from 'react';
import { View, Text, FlatList, StyleSheet, TouchableOpacity, AppState, AppStateStatus, ActivityIndicator, Alert } from 'react-native';
import { useFocusEffect } from '@react-navigation/native';
import { fetchBills, fetchInstances, payBill, deleteBillTemplate } from '../services/api';

export default function DashboardScreen({ navigation }: any) {
  const [bills, setBills] = useState<any[]>([]);
  const [loading, setLoading] = useState(true);
  const [appState, setAppState] = useState(AppState.currentState);

  // Clear data on background
  useEffect(() => {
    const subscription = AppState.addEventListener('change', (nextAppState: AppStateStatus) => {
      if (appState.match(/active/) && nextAppState !== 'active') {
        // App is going to background, clear data!
        setBills([]);
      }
      setAppState(nextAppState);
    });

    return () => {
      subscription.remove();
    };
  }, [appState]);

  const loadData = async () => {
    try {
      setLoading(true);
      const [billsData, instancesData] = await Promise.all([
        fetchBills(),
        fetchInstances()
      ]);
      
      const mappedBills = billsData.map((bill: any) => {
        const activeInstance = instancesData.find((inst: any) => inst.bill_id === bill.id && inst.status !== 'paid');
        return { ...bill, activeInstance };
      });

      mappedBills.sort((a: any, b: any) => {
        const dateA = a.activeInstance ? a.activeInstance.due_date : '9999-12-31';
        const dateB = b.activeInstance ? b.activeInstance.due_date : '9999-12-31';
        return dateA.localeCompare(dateB);
      });

      setBills(mappedBills);
    } catch (e) {
      console.error(e);
      Alert.alert('Error', 'Could not fetch bills. Check your API settings.');
    } finally {
      setLoading(false);
    }
  };

  useFocusEffect(
    useCallback(() => {
      loadData();
    }, [])
  );

  const handlePay = async (instanceId: string, amount: number) => {
    try {
      await payBill(instanceId, amount);
      loadData();
    } catch (e) {
      Alert.alert('Error', 'Failed to mark as paid');
    }
  };

  const handleDelete = async (billId: string) => {
    Alert.alert('Delete Bill', 'Are you sure you want to delete this bill template and all instances?', [
      { text: 'Cancel', style: 'cancel' },
      { text: 'Delete', style: 'destructive', onPress: async () => {
        try {
          await deleteBillTemplate(billId);
          loadData();
        } catch(e) {
          Alert.alert('Error', 'Failed to delete bill');
        }
      }}
    ]);
  };

  const renderItem = ({ item }: { item: any }) => {
    const { activeInstance } = item;
    const amount = activeInstance ? activeInstance.amount_expected : item.default_amount;
    const dueDate = activeInstance ? activeInstance.due_date : '-';
    const status = activeInstance ? activeInstance.status : 'No Active';

    return (
      <View style={styles.card}>
        <View style={styles.headerRow}>
          <TouchableOpacity onPress={() => navigation.navigate('BillDetails', { billId: item.id, billName: item.name })}>
            <Text style={styles.title}>{item.name}</Text>
          </TouchableOpacity>
          <Text style={styles.amount}>${amount.toFixed(2)}</Text>
        </View>
        <Text style={styles.subtitle}>Due: {dueDate} • {item.recurrence_rule}</Text>
        <Text style={styles.status}>Status: {status}</Text>
        <View style={styles.actions}>
          {activeInstance && activeInstance.status !== 'paid' && (
            <TouchableOpacity style={styles.payBtn} onPress={() => handlePay(activeInstance.id, amount)}>
              <Text style={styles.btnText}>Mark Paid</Text>
            </TouchableOpacity>
          )}
          <TouchableOpacity style={styles.delBtn} onPress={() => handleDelete(item.id)}>
            <Text style={styles.btnText}>Delete</Text>
          </TouchableOpacity>
        </View>
      </View>
    );
  };

  if (loading) {
    return <View style={styles.center}><ActivityIndicator size="large" /></View>;
  }

  return (
    <View style={styles.container}>
      <FlatList
        data={bills}
        keyExtractor={(item) => item.id}
        renderItem={renderItem}
        contentContainerStyle={{ paddingBottom: 20 }}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#f0f4f8', padding: 10 },
  center: { flex: 1, justifyContent: 'center', alignItems: 'center' },
  card: { backgroundColor: 'white', padding: 15, borderRadius: 10, marginBottom: 10, elevation: 2, shadowColor: '#000', shadowOpacity: 0.1, shadowRadius: 4, shadowOffset: { width: 0, height: 2 } },
  headerRow: { flexDirection: 'row', justifyContent: 'space-between', marginBottom: 5 },
  title: { fontSize: 18, fontWeight: 'bold', color: '#0056b3' },
  amount: { fontSize: 18, fontWeight: 'bold' },
  subtitle: { color: '#666', marginBottom: 5 },
  status: { color: '#333', marginBottom: 10, textTransform: 'capitalize' },
  actions: { flexDirection: 'row', gap: 10 },
  payBtn: { backgroundColor: '#28a745', padding: 10, borderRadius: 5, flex: 1, alignItems: 'center' },
  delBtn: { backgroundColor: '#dc3545', padding: 10, borderRadius: 5, flex: 1, alignItems: 'center' },
  btnText: { color: 'white', fontWeight: 'bold' }
});
