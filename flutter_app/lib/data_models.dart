import 'dart:async';
import 'package:flutter/material.dart';
import 'package:firebase_database/firebase_database.dart';

class Meal {
  final String id;
  final String name;
  final TimeOfDay time;
  final int amount;

  Meal({
    required this.id,
    required this.name,
    required this.time,
    required this.amount,
  });

  int get minutesSinceMidnight => time.hour * 60 + time.minute;

  DateTime toDateTimeToday() {
    final now = DateTime.now();
    return DateTime(now.year, now.month, now.day, time.hour, time.minute);
  }

  Map<String, dynamic> toFirebaseMap() {
    final hh = time.hour.toString().padLeft(2, '0');
    final mm = time.minute.toString().padLeft(2, '0');

    return {"meal_name": name, "hour": "$hh:$mm", "amount_grams": amount};
  }

  static Meal fromFirebaseMap(String id, Map<dynamic, dynamic> map) {
    final name = (map["meal_name"] ?? "Meal").toString();
    final amount = (map["amount_grams"] ?? 0) as int;

    final hourStr = (map["hour"] ?? "00:00").toString();
    final parts = hourStr.split(':');
    final h = parts.isNotEmpty ? int.tryParse(parts[0]) ?? 0 : 0;
    final m = parts.length > 1 ? int.tryParse(parts[1]) ?? 0 : 0;

    return Meal(
      id: id,
      name: name,
      amount: amount,
      time: TimeOfDay(hour: h, minute: m),
    );
  }
}

class MealProvider extends ChangeNotifier {
  final DatabaseReference _feedingsRef = FirebaseDatabase.instance.ref(
    "feedings",
  );

  StreamSubscription<DatabaseEvent>? _sub;
  List<Meal> _meals = [];

  MealProvider(List<Meal> initialMeals) {
    _startListening();
  }

  void _startListening() {
    _sub?.cancel();
    _sub = _feedingsRef.onValue.listen((event) {
      final data = event.snapshot.value;

      if (data == null) {
        _meals = [];
        notifyListeners();
        return;
      }

      final loaded = <Meal>[];

      if (data is List) {
        for (int i = 0; i < data.length; i++) {
          final value = data[i];
          if (value is Map<dynamic, dynamic>) {
            loaded.add(Meal.fromFirebaseMap(i.toString(), value));
          }
        }
      } else if (data is Map<dynamic, dynamic>) {
        data.forEach((key, value) {
          if (value is Map<dynamic, dynamic>) {
            loaded.add(Meal.fromFirebaseMap(key.toString(), value));
          }
        });
      } else {
        _meals = [];
        notifyListeners();
        return;
      }

      loaded.sort(
        (a, b) => a.minutesSinceMidnight.compareTo(b.minutesSinceMidnight),
      );

      _meals = loaded;
      notifyListeners();
    });
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }

  List<Meal> get meals => List.unmodifiable(_meals);

  Meal? get nextFeeding {
    if (_meals.isEmpty) return null;

    final now = DateTime.now();
    final futureToday = _meals.where((meal) {
      return meal.toDateTimeToday().isAfter(now);
    }).toList();

    if (futureToday.isNotEmpty) return futureToday.first;
    return _meals.first;
  }

  Future<void> addMeal({
    required String name,
    required TimeOfDay time,
    required int amount,
  }) async {
    if (_meals.length >= 6) return;

    final usedIds = _meals
        .map((m) => int.tryParse(m.id))
        .whereType<int>()
        .toSet();
    int nextIndex = 0;
    while (usedIds.contains(nextIndex)) {
      nextIndex++;
    }

    final id = nextIndex.toString();
    final meal = Meal(id: id, name: name, time: time, amount: amount);

    await _feedingsRef.child(id).set(meal.toFirebaseMap());
  }

  Future<void> deleteMeal(String id) async {
    await _feedingsRef.child(id).remove();
  }

  Future<void> feedNow(Meal nextMeal) async {
    await FirebaseDatabase.instance.ref("feedings/commands/feedNow").set({
      "meal_id": nextMeal.id,
      "amount_grams": nextMeal.amount,
      "time": ServerValue.timestamp,
    });
  }
}
