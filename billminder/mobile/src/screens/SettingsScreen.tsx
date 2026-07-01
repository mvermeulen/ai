import React, { useState, useEffect } from 'react';
import { View, Text, TextInput, TouchableOpacity, StyleSheet, Alert } from 'react-native';
import { getApiUrl, setApiUrl } from '../services/api';

export default function SettingsScreen() {
  const [url, setUrl] = useState('');

  useEffect(() => {
    getApiUrl().then(setUrl);
  }, []);

  const handleSave = async () => {
    if (!url) {
      Alert.alert('Error', 'Please enter a valid URL');
      return;
    }
    await setApiUrl(url);
    Alert.alert('Success', 'Settings saved!');
  };

  return (
    <View style={styles.container}>
      <Text style={styles.label}>BillMinder API Server URL</Text>
      <TextInput
        style={styles.input}
        value={url}
        onChangeText={setUrl}
        placeholder="http://192.168.1.x:8080/api"
        autoCapitalize="none"
        keyboardType="url"
      />
      <Text style={styles.helpText}>Enter the local IP address and port of your BillMinder backend. Must start with http:// or https:// and end with /api</Text>
      
      <TouchableOpacity style={styles.btn} onPress={handleSave}>
        <Text style={styles.btnText}>Save Settings</Text>
      </TouchableOpacity>
    </View>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, padding: 20, backgroundColor: '#f0f4f8' },
  label: { fontSize: 16, fontWeight: 'bold', marginBottom: 10 },
  input: { backgroundColor: 'white', padding: 15, borderRadius: 5, borderWidth: 1, borderColor: '#ccc', marginBottom: 10 },
  helpText: { fontSize: 12, color: '#666', marginBottom: 20 },
  btn: { backgroundColor: '#0056b3', padding: 15, borderRadius: 5, alignItems: 'center' },
  btnText: { color: 'white', fontWeight: 'bold', fontSize: 16 }
});
