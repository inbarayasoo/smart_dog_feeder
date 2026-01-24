import 'dart:math';
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import 'stats_provider.dart';

class StatisticsScreen extends StatefulWidget {
  const StatisticsScreen({super.key});

  @override
  State<StatisticsScreen> createState() => _StatisticsScreenState();
}

class _StatisticsScreenState extends State<StatisticsScreen> {
  TabController? _tabController;
  BuildContext? _providerContext;

  @override
  void dispose() {
    _tabController?.removeListener(_onTabChanged);
    super.dispose();
  }

  void _attachControllerIfNeeded(BuildContext innerCtx) {
    _providerContext = innerCtx;
    final ctrl = DefaultTabController.of(innerCtx);
    if (ctrl == null) return;
    if (_tabController == ctrl) return;

    _tabController?.removeListener(_onTabChanged);
    _tabController = ctrl;
    _tabController?.addListener(_onTabChanged);

    WidgetsBinding.instance.addPostFrameCallback((_) => _maybePrune());
  }

  void _onTabChanged() {
    if (_tabController?.indexIsChanging ?? false) return;
    _maybePrune();
  }

  void _maybePrune() {
    final ctrl = _tabController;
    if (ctrl == null) return;
    if (ctrl.index != 0) return;

    final ctx = _providerContext;
    if (ctx == null) return;
    final stats = ctx.read<StatsProvider>();
    stats.pruneWeightsOlderThanAWeek();
  }

  static const Color _brandBlue = Color(0xFF1E3A8A);
  static const Color _brandRed = Color(0xFFDC2626);
  static const Color _bg = Color(0xFFF8FAFC);
  static const Color _card = Colors.white;

  static const String _dogName = "Pedro";

  TextStyle _headline(BuildContext context) => const TextStyle(
    fontSize: 26,
    fontWeight: FontWeight.w800,
    letterSpacing: -0.2,
    color: _brandBlue,
  );

  TextStyle _subhead(BuildContext context) => TextStyle(
    fontSize: 14.5,
    fontWeight: FontWeight.w500,
    color: Colors.grey.shade700,
  );

  TextStyle _sectionTitle(BuildContext context) => const TextStyle(
    fontSize: 18,
    fontWeight: FontWeight.w800,
    letterSpacing: -0.2,
    color: _brandBlue,
  );

  @override
  Widget build(BuildContext context) {
    return ChangeNotifierProvider(
      create: (_) => StatsProvider(),
      child: DefaultTabController(
        length: 2,
        child: Builder(
          builder: (context) {
            _attachControllerIfNeeded(context);
            return Scaffold(
              backgroundColor: _bg,
              appBar: AppBar(
                backgroundColor: _brandBlue,
                foregroundColor: Colors.white,
                title: const Text("Statistics about Pedro"),
                centerTitle: true,
                bottom: const TabBar(
                  labelColor: Colors.white,
                  unselectedLabelColor: Colors.white70,
                  indicatorColor: _brandRed,
                  indicatorWeight: 3,
                  tabs: [
                    Tab(text: "Weekly Stats"),
                    Tab(text: "Daily Stats"),
                  ],
                ),
              ),
              body: SafeArea(
                top: false,
                bottom: true,
                child: TabBarView(
                  children: [
                    _WeeklyStatsTab(
                      dogName: _dogName,
                      brandBlue: _brandBlue,
                      brandRed: _brandRed,
                      card: _card,
                      headline: _headline,
                      subhead: _subhead,
                      sectionTitle: _sectionTitle,
                    ),
                    _DailyMealsTab(
                      brandBlue: _brandBlue,
                      brandRed: _brandRed,
                      card: _card,
                      headline: _headline,
                      subhead: _subhead,
                      sectionTitle: _sectionTitle,
                    ),
                  ],
                ),
              ),
            );
          },
        ),
      ),
    );
  }
}

class _WeeklyStatsTab extends StatelessWidget {
  final String dogName;
  final Color brandBlue;
  final Color brandRed;
  final Color card;
  final TextStyle Function(BuildContext) headline;
  final TextStyle Function(BuildContext) subhead;
  final TextStyle Function(BuildContext) sectionTitle;

  const _WeeklyStatsTab({
    required this.dogName,
    required this.brandBlue,
    required this.brandRed,
    required this.card,
    required this.headline,
    required this.subhead,
    required this.sectionTitle,
  });

  String _dowLabel(DateTime d) {
    const names = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"];
    return names[d.weekday - 1];
  }

  @override
  Widget build(BuildContext context) {
    final stats = context.watch<StatsProvider>();
    final totalsMap = stats.last7DaysTotals();

    final days = totalsMap.keys.toList()..sort((a, b) => a.compareTo(b));
    final vals = days.map((d) => totalsMap[d] ?? 0.0).toList();

    final todayTotal = stats.todayTotal();
    final avg = stats.last7DaysAverage();

    return Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        children: [
          const SizedBox(height: 10),
          Text("Weekly Statistics", style: headline(context)),
          const SizedBox(height: 6),
          Text(
            "Here are the stats for the last 7 days",
            style: subhead(context),
          ),
          const SizedBox(height: 14),
          Expanded(
            child: Container(
              padding: const EdgeInsets.fromLTRB(16, 16, 16, 16),
              decoration: BoxDecoration(
                color: card,
                borderRadius: BorderRadius.circular(18),
                border: Border.all(color: brandBlue.withOpacity(0.12)),
                boxShadow: [
                  BoxShadow(
                    color: Colors.black.withOpacity(0.05),
                    blurRadius: 12,
                    offset: const Offset(0, 6),
                  ),
                ],
              ),
              child: _ScaledBarChart(
                labels: [
                  for (int i = 0; i < days.length; i++)
                    (i == days.length - 1) ? "Today" : _dowLabel(days[i]),
                ],
                values: vals,
                barColor: brandBlue,
                highlightColor: brandRed,
                highlightIndex: vals.isEmpty ? null : vals.length - 1,
                tickStep: 50,
              ),
            ),
          ),
          const SizedBox(height: 12),
          Text(
            "Today $dogName ate ${todayTotal.toStringAsFixed(0)}g",
            style: sectionTitle(context),
          ),
          const SizedBox(height: 6),
          Text(
            "Average of the last 7 days: ${avg.toStringAsFixed(0)}g",
            style: TextStyle(
              color: Colors.grey.shade700,
              fontWeight: FontWeight.w600,
            ),
          ),
          const SizedBox(height: 10),
        ],
      ),
    );
  }
}

class _DailyMealsTab extends StatefulWidget {
  final Color brandBlue;
  final Color brandRed;
  final Color card;
  final TextStyle Function(BuildContext) headline;
  final TextStyle Function(BuildContext) subhead;
  final TextStyle Function(BuildContext) sectionTitle;

  const _DailyMealsTab({
    required this.brandBlue,
    required this.brandRed,
    required this.card,
    required this.headline,
    required this.subhead,
    required this.sectionTitle,
  });

  @override
  State<_DailyMealsTab> createState() => _DailyMealsTabState();
}

class _DailyMealsTabState extends State<_DailyMealsTab> {
  int _selectedWeekday = DateTime.now().weekday;

  String _fmtDate(DateTime d) {
    final y = d.year.toString().padLeft(4, '0');
    final m = d.month.toString().padLeft(2, '0');
    final dd = d.day.toString().padLeft(2, '0');
    return "$y-$m-$dd";
  }

  static const _weekdayNames = <int, String>{
    DateTime.monday: 'Monday',
    DateTime.tuesday: 'Tuesday',
    DateTime.wednesday: 'Wednesday',
    DateTime.thursday: 'Thursday',
    DateTime.friday: 'Friday',
    DateTime.saturday: 'Saturday',
    DateTime.sunday: 'Sunday',
  };

  @override
  Widget build(BuildContext context) {
    final stats = context.watch<StatsProvider>();
    final targetDate = stats.dateForWeekday(_selectedWeekday);
    final rows = stats.mealsForWeekday(_selectedWeekday);

    final dayLabel = _weekdayNames[_selectedWeekday] ?? 'Day';
    final dateLabel = _fmtDate(targetDate);

    return Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        children: [
          const SizedBox(height: 10),
          Text("Daily Statistics", style: widget.headline(context)),
          const SizedBox(height: 6),
          Text(
            "Pick a weekday to view meal consumption",
            style: widget.subhead(context),
          ),
          const SizedBox(height: 14),
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 14, vertical: 8),
            decoration: BoxDecoration(
              color: widget.card,
              borderRadius: BorderRadius.circular(14),
              border: Border.all(color: widget.brandBlue.withOpacity(0.12)),
            ),
            child: Row(
              children: [
                Icon(Icons.calendar_today, color: widget.brandBlue),
                const SizedBox(width: 10),
                Expanded(
                  child: DropdownButtonHideUnderline(
                    child: DropdownButton<int>(
                      value: _selectedWeekday,
                      isExpanded: true,
                      items: _weekdayNames.entries
                          .map(
                            (e) => DropdownMenuItem<int>(
                              value: e.key,
                              child: Text(
                                '${e.value}  •  ${_fmtDate(stats.dateForWeekday(e.key))}',
                              ),
                            ),
                          )
                          .toList(),
                      onChanged: (v) {
                        if (v == null) return;
                        setState(() => _selectedWeekday = v);
                      },
                    ),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 14),
          if (rows.isEmpty)
            Expanded(
              child: Center(
                child: Container(
                  padding: const EdgeInsets.all(16),
                  decoration: BoxDecoration(
                    color: widget.card,
                    borderRadius: BorderRadius.circular(18),
                    border: Border.all(
                      color: widget.brandBlue.withOpacity(0.12),
                    ),
                  ),
                  child: Text(
                    "No meal weights for $dayLabel ($dateLabel) yet.\n"
                    "(With the new format, each meal should have prev_current_weight + new_current_weight.)",
                    textAlign: TextAlign.center,
                  ),
                ),
              ),
            )
          else
            Expanded(
              child: ListView.separated(
                itemCount: rows.length,
                separatorBuilder: (_, __) => const SizedBox(height: 14),
                itemBuilder: (context, i) {
                  final r = rows[i];
                  final pctReal = r.percent * 100;
                  final barValue = r.percent.clamp(0.0, 1.0);
                  final over = pctReal > 100.0;

                  return Container(
                    padding: const EdgeInsets.all(14),
                    decoration: BoxDecoration(
                      color: widget.card,
                      borderRadius: BorderRadius.circular(18),
                      border: Border.all(
                        color: widget.brandBlue.withOpacity(0.12),
                      ),
                      boxShadow: [
                        BoxShadow(
                          color: Colors.black.withOpacity(0.04),
                          blurRadius: 10,
                          offset: const Offset(0, 5),
                        ),
                      ],
                    ),
                    child: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(
                          "${r.mealName} • ${r.hour}",
                          style: widget.sectionTitle(context),
                        ),
                        const SizedBox(height: 6),
                        Text(
                          "Ate ${r.ateG.toStringAsFixed(0)}g out of ${r.targetG.toStringAsFixed(0)}g",
                          style: TextStyle(
                            color: Colors.grey.shade700,
                            fontWeight: FontWeight.w600,
                          ),
                        ),
                        const SizedBox(height: 12),
                        ClipRRect(
                          borderRadius: BorderRadius.circular(10),
                          child: LinearProgressIndicator(
                            value: barValue,
                            minHeight: 14,
                            backgroundColor: Colors.grey.shade200,
                            color: widget.brandBlue,
                          ),
                        ),
                        const SizedBox(height: 8),
                        Text(
                          "${pctReal.toStringAsFixed(2)}% consumed",
                          style: TextStyle(
                            color: over
                                ? widget.brandRed
                                : Colors.grey.shade700,
                            fontWeight: FontWeight.w700,
                          ),
                        ),
                      ],
                    ),
                  );
                },
              ),
            ),
        ],
      ),
    );
  }
}

class _ScaledBarChart extends StatelessWidget {
  final List<String> labels;
  final List<double> values;
  final Color barColor;
  final Color highlightColor;
  final int? highlightIndex;
  final int tickStep;

  const _ScaledBarChart({
    required this.labels,
    required this.values,
    required this.barColor,
    required this.highlightColor,
    required this.highlightIndex,
    required this.tickStep,
  });

  int _roundUpToStep(double x, int step) {
    final v = x.ceil();
    return ((v + step - 1) ~/ step) * step;
  }

  @override
  Widget build(BuildContext context) {
    if (values.isEmpty) return const SizedBox.shrink();

    final maxValRaw = values.reduce(max);
    final maxVal = max(1, _roundUpToStep(maxValRaw, tickStep));

    final ticks = <int>[];
    for (int t = 0; t <= maxVal; t += tickStep) {
      ticks.add(t);
    }

    return LayoutBuilder(
      builder: (context, c) {
        const double topPad = 52;
        const double yAxisWidth = 46;

        const Color dayGreen = Color(0xFF16A34A);

        final canvasHeight = c.maxHeight;

        return Row(
          children: [
            SizedBox(
              width: yAxisWidth,
              height: canvasHeight,
              child: Stack(
                clipBehavior: Clip.none,
                children: [
                  for (final t in ticks)
                    Positioned(
                      bottom: (t / maxVal) * (canvasHeight - topPad),
                      left: 0,
                      right: 6,
                      child: Text(
                        "$t",
                        textAlign: TextAlign.right,
                        style: TextStyle(
                          fontSize: 11.5,
                          color: Colors.grey.shade700,
                          fontWeight: FontWeight.w700,
                        ),
                      ),
                    ),
                ],
              ),
            ),
            const SizedBox(width: 6),
            Expanded(
              child: SizedBox(
                height: canvasHeight,
                child: Stack(
                  clipBehavior: Clip.none,
                  children: [
                    for (final t in ticks)
                      Positioned(
                        bottom: (t / maxVal) * (canvasHeight - topPad),
                        left: 0,
                        right: 0,
                        child: Container(
                          height: 1,
                          color: Colors.black.withOpacity(0.06),
                        ),
                      ),
                    Align(
                      alignment: Alignment.bottomCenter,
                      child: Row(
                        crossAxisAlignment: CrossAxisAlignment.end,
                        children: List.generate(values.length, (i) {
                          final v = values[i];
                          final rawBarH =
                              (v / maxVal).clamp(0.0, 1.0) *
                              (canvasHeight - topPad);

                          const double minBarH = 12;
                          final barH = rawBarH <= 0 ? minBarH : rawBarH;

                          final isHighlight =
                              (highlightIndex != null && i == highlightIndex);

                          double gramsBottom = barH + 16;

                          final bool tallBar = barH >= 34;
                          final double dayBottom = tallBar ? 10 : -22;

                          if (tallBar) {
                            gramsBottom = max(gramsBottom, dayBottom + 20);
                          }

                          gramsBottom = gramsBottom.clamp(
                            0.0,
                            canvasHeight - 20,
                          );

                          return Expanded(
                            child: Padding(
                              padding: const EdgeInsets.symmetric(
                                horizontal: 8,
                              ),
                              child: SizedBox(
                                height: canvasHeight,
                                child: Stack(
                                  clipBehavior: Clip.none,
                                  alignment: Alignment.bottomCenter,
                                  children: [
                                    Align(
                                      alignment: Alignment.bottomCenter,
                                      child: AnimatedContainer(
                                        duration: const Duration(
                                          milliseconds: 250,
                                        ),
                                        height: barH,
                                        decoration: BoxDecoration(
                                          color: isHighlight
                                              ? highlightColor
                                              : barColor,
                                          borderRadius: BorderRadius.circular(
                                            12,
                                          ),
                                        ),
                                      ),
                                    ),
                                    Positioned(
                                      bottom: dayBottom,
                                      child: Text(
                                        labels[i],
                                        style: const TextStyle(
                                          color: dayGreen,
                                          fontWeight: FontWeight.w900,
                                          fontSize: 14,
                                          letterSpacing: 0.2,
                                        ),
                                      ),
                                    ),
                                    Positioned(
                                      bottom: gramsBottom,
                                      child: Text(
                                        "${v.toStringAsFixed(0)}g",
                                        style: TextStyle(
                                          fontSize: 12.5,
                                          fontWeight: FontWeight.w900,
                                          color: isHighlight
                                              ? highlightColor
                                              : Colors.grey.shade800,
                                        ),
                                      ),
                                    ),
                                  ],
                                ),
                              ),
                            ),
                          );
                        }),
                      ),
                    ),
                  ],
                ),
              ),
            ),
          ],
        );
      },
    );
  }
}
