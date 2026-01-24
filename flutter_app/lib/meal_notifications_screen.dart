// ignore_for_file: depend_on_referenced_packages

import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/material.dart';

class MealNotificationsScreen extends StatelessWidget {
  const MealNotificationsScreen({super.key});

  IconData _iconFor(String type) {
    switch (type) {
      case 'feeding_started':
        return Icons.restaurant;
      case 'feeding_success':
        return Icons.check_circle;
      case 'feeding_failed_timeout':
        return Icons.warning_amber_rounded;
      case 'feeding_stopped_empty':
        return Icons.inventory_2_outlined;
      case 'feeding_stopped_disabled':
        return Icons.block;
      default:
        return Icons.notifications;
    }
  }

  String _primaryText(String type) {
    switch (type) {
      case 'feeding_started':
        return 'Feeding started';
      case 'feeding_success':
        return 'Pedro got his meal';
      case 'feeding_failed_timeout':
        return 'Motor issue — feeding not completed';
      case 'feeding_stopped_empty':
        return 'Feeding stopped — container empty';
      case 'feeding_stopped_disabled':
        return 'Feeding stopped — motor disabled';
      default:
        return 'Meal notification';
    }
  }

  @override
  Widget build(BuildContext context) {
    final now = DateTime.now();
    final today =
        '${now.year.toString().padLeft(4, '0')}-${now.month.toString().padLeft(2, '0')}-${now.day.toString().padLeft(2, '0')}';

    final ref = FirebaseDatabase.instance
        .ref('logs/meal_notifications/$today')
        .orderByChild('ts')
        .limitToLast(200);

    return Scaffold(
      appBar: AppBar(title: const Text('Meal Notifications')),
      body: StreamBuilder<DatabaseEvent>(
        stream: ref.onValue,
        builder: (context, snap) {
          if (snap.hasError) {
            return Center(child: Text('Error: ${snap.error}'));
          }

          final v = snap.data?.snapshot.value;

          if (!snap.hasData || v == null) {
            return const Center(child: Text('No notifications yet'));
          }

          final items = <Map<String, dynamic>>[];

          if (v is List) {
            for (int i = 0; i < v.length; i++) {
              final raw = v[i];
              if (raw is Map) {
                final m = raw.cast<String, dynamic>();
                m['_id'] = i.toString();
                items.add(m);
              }
            }
          } else if (v is Map) {
            v.forEach((k, raw) {
              if (raw is Map) {
                final m = raw.cast<String, dynamic>();
                m['_id'] = k.toString();
                items.add(m);
              }
            });
          } else {
            return Center(
              child: Text('Unexpected data type: ${v.runtimeType}'),
            );
          }

          if (items.isEmpty) {
            return const Center(child: Text('No notifications yet'));
          }

          items.sort((a, b) {
            final ta = (a['ts'] is num) ? (a['ts'] as num).toInt() : 0;
            final tb = (b['ts'] is num) ? (b['ts'] as num).toInt() : 0;
            return tb.compareTo(ta);
          });

          return ListView.separated(
            padding: const EdgeInsets.symmetric(vertical: 8),
            itemCount: items.length,
            separatorBuilder: (_, __) => const Divider(height: 1),
            itemBuilder: (context, i) {
              final it = items[i];

              final type = (it['type'] ?? '').toString();
              final meal = (it['meal_name'] ?? '').toString().trim();
              final hh = (it['hour'] is num) ? (it['hour'] as num).toInt() : -1;
              final mm = (it['minute'] is num)
                  ? (it['minute'] as num).toInt()
                  : -1;

              final timeStr = (hh >= 0 && mm >= 0)
                  ? '${hh.toString().padLeft(2, '0')}:${mm.toString().padLeft(2, '0')}'
                  : '';

              final mealLabel = meal.isEmpty ? 'Meal' : meal;

              return ListTile(
                leading: CircleAvatar(child: Icon(_iconFor(type))),
                title: RichText(
                  text: TextSpan(
                    style: DefaultTextStyle.of(context).style,
                    children: [
                      TextSpan(
                        text: _primaryText(type),
                        style: const TextStyle(fontWeight: FontWeight.w600),
                      ),
                      const TextSpan(text: ' • '),
                      TextSpan(
                        text: mealLabel,
                        style: const TextStyle(fontWeight: FontWeight.w700),
                      ),
                      if (timeStr.isNotEmpty) ...[
                        const TextSpan(text: ' • '),
                        TextSpan(text: timeStr),
                      ],
                    ],
                  ),
                ),
              );
            },
          );
        },
      ),
    );
  }
}
