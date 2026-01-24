// ignore_for_file: depend_on_referenced_packages

import 'dart:async';

import 'package:firebase_core/firebase_core.dart';
import 'package:firebase_database/firebase_database.dart';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';

import 'data_models.dart';
import 'statistics_screen.dart';
import 'meal_notifications_screen.dart';

class _HomeSession {
  static int lastShownContainerEmptyEventId = -1;
  static bool isDialogOpen = false;
}

class HomeScreen extends StatefulWidget {
  const HomeScreen({super.key});

  @override
  State<HomeScreen> createState() => _HomeScreenState();
}

class _HomeScreenState extends State<HomeScreen> {
  static const String _containerStatusPath = 'status/container';

  StreamSubscription<DatabaseEvent>? _containerSub;

  bool _latestEmpty = false;
  int _latestEventId = -1;

  DatabaseReference get _containerRef =>
      FirebaseDatabase.instance.ref(_containerStatusPath);

  @override
  void initState() {
    super.initState();

    try {
      Firebase.app();
    } catch (_) {}

    _startContainerListener();
  }

  void _startContainerListener() {
    _containerSub?.cancel();

    _containerSub = _containerRef.onValue.listen((event) {
      final val = event.snapshot.value;

      if (val is! Map) {
        return;
      }

      final empty = val['empty'] == true;

      final dynamic evRaw = val['eventId'];
      final int eventId = (evRaw is int)
          ? evRaw
          : (evRaw is num)
          ? evRaw.toInt()
          : int.tryParse(evRaw?.toString() ?? '') ?? -1;

      _latestEmpty = empty;
      _latestEventId = eventId;

      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (!mounted) return;
        _maybeShowContainerEmptyDialog();
      });
    }, onError: (_) {});
  }

  @override
  void dispose() {
    _containerSub?.cancel();
    super.dispose();
  }

  void _showWifiManagerInfoDialog() {
    showDialog<void>(
      context: context,
      builder: (ctx) => AlertDialog(
        title: const Text('How to connect (WiFi Manager)'),
        content: const Text(
          "1. Open your Wi-Fi settings and look for a network named Feeder_Setup.\n"
          "2. Tap it and enter the password FeedMeNow123!, then open Google (or any browser) and enter: 192.168.4.1\n"
          "3. The WiFi Manager page will open. Choose your Personal Hotspot or ICST.\n"
          "4. Enter the password arduino123 for ICST, or your own password for the Personal Hotspot.\n"
          "5. Connected successfully — the ESP is now connected to Wi-Fi.",
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.of(ctx).pop(),
            child: const Text('OK'),
          ),
        ],
      ),
    );
  }

  Future<void> _maybeShowContainerEmptyDialog() async {
    if (!_latestEmpty) return;
    if (_latestEventId < 0) return;

    final isCurrentRoute = ModalRoute.of(context)?.isCurrent ?? true;
    if (!isCurrentRoute) return;

    if (_HomeSession.lastShownContainerEmptyEventId == _latestEventId) return;

    if (_HomeSession.isDialogOpen) return;

    _HomeSession.isDialogOpen = true;
    _HomeSession.lastShownContainerEmptyEventId = _latestEventId;

    if (!mounted) {
      _HomeSession.isDialogOpen = false;
      return;
    }

    await showDialog<void>(
      context: context,
      barrierDismissible: false,
      builder: (ctx) => WillPopScope(
        onWillPop: () async => false,
        child: AlertDialog(
          title: const Text('Container Empty'),
          content: const Text('The container is empty.please refill !'),
          actions: [
            TextButton(
              onPressed: () => Navigator.of(ctx).pop(),
              child: const Text('Ok'),
            ),
          ],
        ),
      ),
    );

    _HomeSession.isDialogOpen = false;
  }

  @override
  Widget build(BuildContext context) {
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (!mounted) return;
      _maybeShowContainerEmptyDialog();
    });

    return Consumer<MealProvider>(
      builder: (context, mealProvider, child) {
        final nextMeal = mealProvider.nextFeeding;
        final hour = nextMeal?.time.hourOfPeriod ?? '00';
        final minute = nextMeal?.time.minute.toString().padLeft(2, '0') ?? '00';
        final period = nextMeal?.time.period == DayPeriod.am ? 'AM' : 'PM';
        final nextTime = '$hour:$minute $period';

        return Scaffold(
          appBar: AppBar(
            title: const Text('Pet Feeder'),
            automaticallyImplyLeading: false,
            actions: [
              IconButton(
                tooltip: 'WiFi setup help',
                icon: const Icon(Icons.info_outline),
                onPressed: _showWifiManagerInfoDialog,
              ),
            ],
          ),
          body: Center(
            child: Padding(
              padding: const EdgeInsets.all(24.0),
              child: Column(
                mainAxisAlignment: MainAxisAlignment.start,
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: <Widget>[
                  Card(
                    color: Theme.of(context).colorScheme.primary.withAlpha(229),
                    child: Padding(
                      padding: const EdgeInsets.all(32.0),
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            'Welcome to Pet Feeder',
                            style: Theme.of(context).textTheme.headlineMedium!
                                .copyWith(color: Colors.white, fontSize: 28),
                          ),
                          const SizedBox(height: 20),
                          Text(
                            'Next Feeding: ${nextMeal != null ? nextTime : 'No Schedule'}',
                            style: const TextStyle(
                              color: Colors.white70,
                              fontSize: 20,
                              fontWeight: FontWeight.w500,
                            ),
                          ),
                          if (nextMeal != null)
                            Padding(
                              padding: const EdgeInsets.only(top: 8.0),
                              child: Text(
                                '• Amount: ${nextMeal.amount} g',
                                style: const TextStyle(
                                  color: Colors.white,
                                  fontSize: 18,
                                ),
                              ),
                            ),
                        ],
                      ),
                    ),
                  ),
                  const SizedBox(height: 40),
                  ListTile(
                    leading: const Icon(
                      Icons.calendar_month,
                      color: Colors.indigo,
                    ),
                    title: const Text('View Schedule'),
                    trailing: const Icon(Icons.arrow_forward_ios),
                    onTap: () => Navigator.pushNamed(context, '/schedule'),
                  ),
                  const Divider(),
                  ListTile(
                    leading: const Icon(Icons.bar_chart, color: Colors.indigo),
                    title: const Text('View Statistics'),
                    trailing: const Icon(Icons.arrow_forward_ios),
                    onTap: () {
                      Navigator.push(
                        context,
                        MaterialPageRoute(
                          builder: (_) => const StatisticsScreen(),
                        ),
                      );
                    },
                  ),
                  const Divider(),
                  ListTile(
                    leading: const Icon(
                      Icons.notifications,
                      color: Colors.indigo,
                    ),
                    title: const Text('Meal Notifications'),
                    trailing: const Icon(Icons.arrow_forward_ios),
                    onTap: () {
                      Navigator.push(
                        context,
                        MaterialPageRoute(
                          builder: (_) => const MealNotificationsScreen(),
                        ),
                      );
                    },
                  ),
                  const SizedBox(height: 24),
                  Center(
                    child: Container(
                      decoration: BoxDecoration(
                        border: Border.all(color: Colors.black, width: 2),
                        borderRadius: BorderRadius.circular(16),
                      ),
                      child: ClipRRect(
                        borderRadius: BorderRadius.circular(14),
                        child: Image.asset(
                          'assets/images/dog.png',
                          height: 180,
                          fit: BoxFit.contain,
                          errorBuilder: (context, error, stackTrace) =>
                              const Padding(
                                padding: EdgeInsets.all(24.0),
                                child: Icon(Icons.pets, size: 80),
                              ),
                        ),
                      ),
                    ),
                  ),
                ],
              ),
            ),
          ),
        );
      },
    );
  }
}
