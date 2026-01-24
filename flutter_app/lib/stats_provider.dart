import 'dart:async';
import 'package:flutter/material.dart';
import 'package:firebase_database/firebase_database.dart';

class WeightEntry {
  final String id;
  final String day;
  final String date;
  final String hour;
  final String mealName;
  final double amountGrams;
  final double prevCurrentWeight;
  final double newCurrentWeight;

  WeightEntry({
    required this.id,
    required this.day,
    required this.date,
    required this.hour,
    required this.mealName,
    required this.amountGrams,
    required this.prevCurrentWeight,
    required this.newCurrentWeight,
  });
}

class MealConsumptionRow {
  final String day;
  final String date;
  final String hour;
  final String mealName;
  final double ateG;
  final double targetG;
  final double percent;

  MealConsumptionRow({
    required this.day,
    required this.date,
    required this.hour,
    required this.mealName,
    required this.ateG,
    required this.targetG,
    required this.percent,
  });
}

class StatsProvider extends ChangeNotifier {
  final DatabaseReference _weightsRef = FirebaseDatabase.instance.ref(
    'weights',
  );
  StreamSubscription<DatabaseEvent>? _sub;
  bool _pruneInProgress = false;

  List<WeightEntry> _weights = [];
  List<WeightEntry> get weights => List.unmodifiable(_weights);

  StatsProvider() {
    _startListening();
  }

  @override
  void dispose() {
    _sub?.cancel();
    super.dispose();
  }

  void _startListening() {
    _sub?.cancel();
    _sub = _weightsRef.onValue.listen((event) {
      final data = event.snapshot.value;

      if (data == null) {
        _weights = [];
        notifyListeners();
        return;
      }

      final loaded = <WeightEntry>[];

      double readDouble(dynamic x) {
        if (x == null) return 0.0;
        if (x is num) return x.toDouble();
        return double.tryParse(x.toString()) ?? 0.0;
      }

      String readStr(dynamic x) => (x ?? '').toString();

      WeightEntry? fromMap(String id, Map<dynamic, dynamic> m) {
        final day = readStr(m['day']).trim().toLowerCase();
        final date = readStr(m['date']).trim();
        final hour = readStr(m['hour']).trim();
        final mealName = readStr(m['meal_name']).trim();

        if (day.isEmpty || date.isEmpty || hour.isEmpty || mealName.isEmpty) {
          return null;
        }

        final amount = readDouble(m['amount_grams']);

        final hasNew =
            m.containsKey('prev_current_weight') ||
            m.containsKey('new_current_weight');
        if (hasNew) {
          return WeightEntry(
            id: id,
            day: day,
            date: date,
            hour: hour,
            mealName: mealName,
            amountGrams: amount,
            prevCurrentWeight: readDouble(m['prev_current_weight']),
            newCurrentWeight: readDouble(m['new_current_weight']),
          );
        }

        final cw = readDouble(m['current_weight']);
        return WeightEntry(
          id: id,
          day: day,
          date: date,
          hour: hour,
          mealName: mealName,
          amountGrams: amount,
          prevCurrentWeight: cw,
          newCurrentWeight: cw,
        );
      }

      if (data is List) {
        for (int i = 0; i < data.length; i++) {
          final v = data[i];
          if (v is Map<dynamic, dynamic>) {
            final w = fromMap(i.toString(), v);
            if (w != null) loaded.add(w);
          }
        }
      } else if (data is Map<dynamic, dynamic>) {
        data.forEach((key, value) {
          if (value is Map<dynamic, dynamic>) {
            final w = fromMap(key.toString(), value);
            if (w != null) loaded.add(w);
          }
        });
      }

      _weights = loaded;
      notifyListeners();
    });
  }

  String _fmtDate(DateTime d) {
    final y = d.year.toString().padLeft(4, '0');
    final m = d.month.toString().padLeft(2, '0');
    final dd = d.day.toString().padLeft(2, '0');
    return '$y-$m-$dd';
  }

  DateTime? _parseDate(String s) {
    try {
      final parts = s.split('-');
      if (parts.length != 3) return null;
      final y = int.parse(parts[0]);
      final m = int.parse(parts[1]);
      final d = int.parse(parts[2]);
      return DateTime(y, m, d);
    } catch (_) {
      return null;
    }
  }

  List<MealConsumptionRow> _allMealRows() {
    final rows = <MealConsumptionRow>[];

    for (final w in _weights) {
      final eatenRaw = w.prevCurrentWeight - w.newCurrentWeight;
      final eaten = eatenRaw.isFinite ? eatenRaw : 0.0;
      final ateG = eaten < 0 ? 0.0 : eaten;

      final target = w.amountGrams <= 0 ? 0.0 : w.amountGrams;
      final pct = target <= 0 ? 0.0 : (ateG / target);

      rows.add(
        MealConsumptionRow(
          day: w.day,
          date: w.date,
          hour: w.hour,
          mealName: w.mealName,
          ateG: ateG,
          targetG: target,
          percent: pct,
        ),
      );
    }

    return rows;
  }

  List<MealConsumptionRow> mealsForDate(DateTime date) {
    final dateStr = _fmtDate(date);
    final rows = _allMealRows().where((r) => r.date == dateStr).toList();
    rows.sort((a, b) => a.hour.compareTo(b.hour));
    return rows;
  }

  List<MealConsumptionRow> mealsForWeekday(int weekday) {
    final now = DateTime.now();
    final today = DateTime(now.year, now.month, now.day);
    final diff = (today.weekday - weekday) % 7;
    final target = today.subtract(Duration(days: diff));
    return mealsForDate(target);
  }

  DateTime dateForWeekday(int weekday) {
    final now = DateTime.now();
    final today = DateTime(now.year, now.month, now.day);
    final diff = (today.weekday - weekday) % 7;
    return today.subtract(Duration(days: diff));
  }

  double totalForDateStr(String dateStr) {
    double sum = 0.0;
    for (final r in _allMealRows()) {
      if (r.date == dateStr) sum += r.ateG;
    }
    return sum;
  }

  Map<DateTime, double> last7DaysTotals() {
    final now = DateTime.now();
    final today = DateTime(now.year, now.month, now.day);

    final out = <DateTime, double>{};
    for (int k = 6; k >= 0; k--) {
      final d = today.subtract(Duration(days: k));
      out[d] = totalForDateStr(_fmtDate(d));
    }
    return out;
  }

  double todayTotal() {
    final now = DateTime.now();
    final today = DateTime(now.year, now.month, now.day);
    return totalForDateStr(_fmtDate(today));
  }

  double last7DaysAverage() {
    final vals = last7DaysTotals().values.toList();
    if (vals.isEmpty) return 0.0;
    final sum = vals.fold<double>(0.0, (a, b) => a + b);
    return sum / vals.length;
  }

  Future<void> pruneWeightsOlderThanAWeek() async {
    if (_pruneInProgress) return;
    _pruneInProgress = true;
    try {
      final snap = await _weightsRef.get();
      final data = snap.value;
      if (data == null) return;

      final now = DateTime.now();
      final today = DateTime(now.year, now.month, now.day);
      final cutoff = today.subtract(const Duration(days: 6));

      Future<void> maybeRemove(String key, Map<dynamic, dynamic> m) async {
        final dateStr = (m['date'] ?? '').toString().trim();
        final dt = _parseDate(dateStr);
        if (dt == null) return;
        final d0 = DateTime(dt.year, dt.month, dt.day);
        if (d0.isBefore(cutoff)) {
          await _weightsRef.child(key).remove();
        }
      }

      if (data is List) {
        for (int i = 0; i < data.length; i++) {
          final v = data[i];
          if (v is Map<dynamic, dynamic>) {
            await maybeRemove(i.toString(), v);
          }
        }
      } else if (data is Map<dynamic, dynamic>) {
        for (final e in data.entries) {
          final key = e.key.toString();
          final v = e.value;
          if (v is Map<dynamic, dynamic>) {
            await maybeRemove(key, v);
          }
        }
      }
    } finally {
      _pruneInProgress = false;
    }
  }
}
