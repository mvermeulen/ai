import React from 'react';
import { NavigationContainer } from '@react-navigation/native';
import { createNativeStackNavigator } from '@react-navigation/native-stack';
import { TouchableOpacity, Text } from 'react-native';

import DashboardScreen from './src/screens/DashboardScreen';
import BillDetailsScreen from './src/screens/BillDetailsScreen';
import SettingsScreen from './src/screens/SettingsScreen';

const Stack = createNativeStackNavigator();

export default function App() {
  return (
    <NavigationContainer>
      <Stack.Navigator initialRouteName="Dashboard">
        <Stack.Screen 
          name="Dashboard" 
          component={DashboardScreen} 
          options={({ navigation }) => ({
            title: 'BillMinder',
            headerRight: () => (
              <TouchableOpacity onPress={() => navigation.navigate('Settings')} style={{ marginRight: 10 }}>
                <Text style={{ fontSize: 24 }}>⚙️</Text>
              </TouchableOpacity>
            )
          })}
        />
        <Stack.Screen 
          name="BillDetails" 
          component={BillDetailsScreen} 
          options={({ route }: any) => ({ title: route.params?.billName || 'Details' })} 
        />
        <Stack.Screen 
          name="Settings" 
          component={SettingsScreen} 
          options={{ title: 'Settings' }} 
        />
      </Stack.Navigator>
    </NavigationContainer>
  );
}
