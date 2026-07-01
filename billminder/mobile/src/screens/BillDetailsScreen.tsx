import React, { useState, useEffect, useCallback } from 'react';
import { View, Text, StyleSheet, FlatList, ActivityIndicator, Alert, TouchableOpacity } from 'react-native';
import { fetchBillDetails, fetchBillInstances, payBill } from '../services/api';
import { useFocusEffect } from '@react-navigation/native';

export default function BillDetailsScreen({ route }: any) {
  const { billId, billName } = route.params;
  const [bill, setBill] = useState<any>(null);
  const [instances, setInstances] = useState<any[]>([]);
  const [loading, setLoading] = useState(true);

  const loadData = async () => {
    try {
      setLoading(true);
      const [billData, instancesData] = await Promise.all([
        fetchBillDetails(billId),
        fetchBillInstances(billId)
      ]);
      setBill(billData);
      setInstances(instancesData);
    } catch (e) {
      Alert.alert('Error', 'Failed to load details');
    } finally {
      setLoading(false);
    }
  };

  useFocusEffect(
    useCallback(() => {
      loadData();
    }, [billId])
  );

  const handlePay = async (instanceId: string, amount: number) => {
    try {
      await payBill(instanceId, amount);
      loadData();
    } catch (e) {
      Alert.alert('Error', 'Failed to mark as paid');
    }
  };

  if (loading) return <View style={styles.center}><ActivityIndicator size="large" /></View>;

  return (
    <View style={styles.container}>
      <View style={styles.metaCard}>
        <Text style={styles.title}>{bill?.name}</Text>
        <Text>URL: {bill?.url || 'N/A'}</Text>
        <Text>Account: {bill?.account || 'N/A'}</Text>
        <Text>Password: {bill?.password ? '********' : 'N/A'}</Text>
      </View>
      
      <Text style={styles.subtitle}>Instances</Text>
      <FlatList
        data={instances}
        keyExtractor={item => item.id}
        renderItem={({ item }) => (
          <View style={styles.instanceCard}>
            <View style={{ flex: 1 }}>
              <Text style={{ fontWeight: 'bold' }}>${item.amount_expected.toFixed(2)}</Text>
              <Text>Due: {item.due_date}</Text>
              <Text style={{ textTransform: 'capitalize', color: item.status === 'paid' ? 'green' : 'black' }}>{item.status}</Text>
            </View>
            {item.status !== 'paid' && (
              <TouchableOpacity style={styles.payBtn} onPress={() => handlePay(item.id, item.amount_expected)}>
                <Text style={styles.btnText}>Pay</Text>
              </TouchableOpacity>
            )}
          </View>
        )}
      />
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, padding: 10, backgroundColor: '#f0f4f8' },
  center: { flex: 1, justifyContent: 'center', alignItems: 'center' },
  metaCard: { backgroundColor: 'white', padding: 15, borderRadius: 10, marginBottom: 20 },
  title: { fontSize: 20, fontWeight: 'bold', marginBottom: 10 },
  subtitle: { fontSize: 18, fontWeight: 'bold', marginBottom: 10, marginLeft: 5 },
  instanceCard: { flexDirection: 'row', backgroundColor: 'white', padding: 15, borderRadius: 5, marginBottom: 10, alignItems: 'center' },
  payBtn: { backgroundColor: '#28a745', padding: 10, borderRadius: 5 },
  btnText: { color: 'white', fontWeight: 'bold' }
});
