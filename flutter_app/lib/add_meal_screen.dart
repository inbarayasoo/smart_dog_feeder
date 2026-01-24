// ignore_for_file: depend_on_referenced_packages

import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'data_models.dart';

class AddMealScreen extends StatefulWidget {
  const AddMealScreen({super.key});

  @override
  State<AddMealScreen> createState() => _AddMealScreenState();
}

class _AddMealScreenState extends State<AddMealScreen> {
  final _formKey = GlobalKey<FormState>();
  String _mealName = 'New Meal';
  int _amount = 100;
  TimeOfDay? _selectedTime;

  static const int minAmount = 10;
  static const int maxAmount = 120;

  Future<void> _selectTime(BuildContext context) async {
    final TimeOfDay? picked = await showTimePicker(
      context: context,
      initialTime: _selectedTime ?? TimeOfDay.now(),
    );
    if (picked != null && picked != _selectedTime) {
      setState(() {
        _selectedTime = picked;
      });
    }
  }

  Future<void> _saveMeal() async {
    if (_formKey.currentState!.validate() && _selectedTime != null) {
      _formKey.currentState!.save();

      final mealProvider = Provider.of<MealProvider>(context, listen: false);

      final now = DateTime.now();
      final newMealDateTime = DateTime(
        now.year,
        now.month,
        now.day,
        _selectedTime!.hour,
        _selectedTime!.minute,
      );

      for (var existingMeal in mealProvider.meals) {
        final existingMealDateTime = existingMeal.toDateTimeToday();
        final diff = newMealDateTime.difference(existingMealDateTime).abs();

        if (diff.inMinutes < 60) {
          _showErrorDialog(
            'Time Conflict',
            'This feeding time is too close to ${existingMeal.name} (${existingMeal.time.format(context)}). Meals must be at least 1 hour apart.',
          );
          return;
        }
      }

      await mealProvider.addMeal(
        name: _mealName,
        time: _selectedTime!,
        amount: _amount,
      );

      if (!mounted) return;

      Navigator.of(context).pop();
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(
            '$_mealName scheduled for ${_selectedTime!.format(context)}!',
          ),
        ),
      );
    } else if (_selectedTime == null) {
      _showErrorDialog(
        'Missing Time',
        'Please select the hour for the feeding time.',
      );
    }
  }

  void _showErrorDialog(String title, String message) {
    showDialog(
      context: context,
      builder: (ctx) => AlertDialog(
        title: Text(title, style: const TextStyle(color: Colors.red)),
        content: Text(message),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(ctx).pop(),
            child: const Text('OK'),
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(title: const Text('Add Feeding Time')),
      body: SingleChildScrollView(
        padding: const EdgeInsets.all(24.0),
        child: Form(
          key: _formKey,
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: <Widget>[
              Text(
                'Amount of food: ${_amount}g',
                style: Theme.of(context).textTheme.headlineSmall,
              ),
              const SizedBox(height: 8),
              Slider(
                value: _amount.toDouble(),
                min: minAmount.toDouble(),
                max: maxAmount.toDouble(),
                divisions: (maxAmount - minAmount) ~/ 5,
                label: '$_amount g',
                onChanged: (double newValue) {
                  setState(() {
                    _amount = newValue.round();
                  });
                },
              ),
              const SizedBox(height: 32),
              Text('Hour:', style: Theme.of(context).textTheme.headlineSmall),
              const SizedBox(height: 8),
              ListTile(
                title: Text(
                  _selectedTime == null
                      ? 'Select Time'
                      : _selectedTime!.format(context),
                  style: TextStyle(
                    fontSize: 18,
                    color: _selectedTime == null
                        ? Colors.grey
                        : Colors.indigo.shade700,
                    fontWeight: _selectedTime == null
                        ? FontWeight.normal
                        : FontWeight.bold,
                  ),
                ),
                trailing: const Icon(Icons.access_time),
                onTap: () => _selectTime(context),
                shape: RoundedRectangleBorder(
                  borderRadius: BorderRadius.circular(8),
                  side: BorderSide(color: Colors.indigo.shade200, width: 1.5),
                ),
              ),
              const SizedBox(height: 32),
              TextFormField(
                initialValue: _mealName,
                maxLength: 20,
                decoration: InputDecoration(
                  labelText: 'Meal Name (e.g., Dinner)',
                  border: OutlineInputBorder(
                    borderRadius: BorderRadius.circular(8),
                  ),
                ),
                onChanged: (value) {
                  setState(() {
                    _mealName = value.trim().isEmpty
                        ? 'New Meal'
                        : value.trim();
                  });
                },
                onSaved: (value) {
                  _mealName = value!.trim().isEmpty ? 'New Meal' : value.trim();
                },
                validator: (value) {
                  if (value == null || value.trim().isEmpty) {
                    return 'Please enter a meal name.';
                  }
                  if (value.trim().length > 20) {
                    return 'Meal name must be 20 characters or less.';
                  }
                  return null;
                },
              ),
              const SizedBox(height: 40),
              Row(
                mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                children: <Widget>[
                  Expanded(
                    child: OutlinedButton(
                      onPressed: () => Navigator.pop(context),
                      style: OutlinedButton.styleFrom(
                        padding: const EdgeInsets.symmetric(vertical: 16),
                        foregroundColor: Colors.red,
                        side: const BorderSide(color: Colors.red),
                      ),
                      child: const Text(
                        'Cancel',
                        style: TextStyle(fontSize: 18),
                      ),
                    ),
                  ),
                  const SizedBox(width: 16),
                  Expanded(
                    child: ElevatedButton(
                      onPressed: _saveMeal,
                      style: ElevatedButton.styleFrom(
                        backgroundColor: Colors.indigo,
                        foregroundColor: Colors.white,
                        padding: const EdgeInsets.symmetric(vertical: 16),
                      ),
                      child: const Text('Save', style: TextStyle(fontSize: 18)),
                    ),
                  ),
                ],
              ),
            ],
          ),
        ),
      ),
    );
  }
}
