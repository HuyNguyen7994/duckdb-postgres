#include "postgres_filter_pushdown.hpp"
#include "duckdb/parser/keyword_helper.hpp"
#include "duckdb/planner/filter/in_filter.hpp"
#include "duckdb/planner/filter/optional_filter.hpp"
#include "duckdb/planner/filter/struct_filter.hpp"
#include "duckdb/common/enum_util.hpp"

namespace duckdb {

string PostgresFilterPushdown::CreateExpression(string &column_name, vector<unique_ptr<TableFilter>> &filters,
                                                string op) {
	vector<string> filter_entries;
	for (auto &filter : filters) {
		filter_entries.push_back(TransformFilter(column_name, *filter));
	}
	return "(" + StringUtil::Join(filter_entries, " " + op + " ") + ")";
}

string PostgresFilterPushdown::TransformComparision(ExpressionType type) {
	switch (type) {
	case ExpressionType::COMPARE_EQUAL:
		return "=";
	case ExpressionType::COMPARE_NOTEQUAL:
		return "<>";
	case ExpressionType::COMPARE_LESSTHAN:
		return "<";
	case ExpressionType::COMPARE_GREATERTHAN:
		return ">";
	case ExpressionType::COMPARE_LESSTHANOREQUALTO:
		return "<=";
	case ExpressionType::COMPARE_GREATERTHANOREQUALTO:
		return ">=";
	default:
		throw NotImplementedException("Unsupported expression type");
	}
}

string PostgresFilterPushdown::TransformFilter(string &column_name, TableFilter &filter) {
	switch (filter.filter_type) {
	case TableFilterType::IS_NULL:
		return column_name + " IS NULL";
	case TableFilterType::IS_NOT_NULL:
		return column_name + " IS NOT NULL";
	case TableFilterType::CONJUNCTION_AND: {
		auto &conjunction_filter = filter.Cast<ConjunctionAndFilter>();
		return CreateExpression(column_name, conjunction_filter.child_filters, "AND");
	}
	case TableFilterType::CONJUNCTION_OR: {
		auto &conjunction_filter = filter.Cast<ConjunctionOrFilter>();
		return CreateExpression(column_name, conjunction_filter.child_filters, "OR");
	}
	case TableFilterType::CONSTANT_COMPARISON: {
		auto &constant_filter = filter.Cast<ConstantFilter>();
		auto constant_string = KeywordHelper::WriteQuoted(constant_filter.constant.ToString());
		auto operator_string = TransformComparision(constant_filter.comparison_type);
		return StringUtil::Format("%s %s %s", column_name, operator_string, constant_string);
	}
	case TableFilterType::STRUCT_EXTRACT: {
		auto &struct_filter = filter.Cast<StructFilter>();
		auto child_name = KeywordHelper::WriteQuoted(struct_filter.child_name, '\"');
		auto new_name = "(" + column_name + ")." + child_name;
		return TransformFilter(new_name, *struct_filter.child_filter);
	}
	case TableFilterType::OPTIONAL_FILTER: {
		auto &optional_filter = filter.Cast<OptionalFilter>();
		return TransformFilter(column_name, *optional_filter.child_filter);
	}
	case TableFilterType::IN_FILTER: {
		auto &in_filter = filter.Cast<InFilter>();
		string in_list;
		for(auto &val : in_filter.values) {
			if (!in_list.empty()) {
				in_list += ", ";
			}
			in_list += KeywordHelper::WriteQuoted(val.ToString());
		}
		return column_name + " IN (" + in_list + ")";
	}
	default:
		throw InternalException("Unsupported table filter type");
	}
}

string PostgresFilterPushdown::TransformFilters(const vector<column_t> &column_ids,
                                                optional_ptr<TableFilterSet> filters, const vector<string> &names) {
	if (!filters || filters->filters.empty()) {
		// no filters
		return string();
	}
	string result;
	for (auto &entry : filters->filters) {
		if (!result.empty()) {
			result += " AND ";
		}
		auto column_name = KeywordHelper::WriteQuoted(names[column_ids[entry.first]], '"');
		auto &filter = *entry.second;

		result += TransformFilter(column_name, filter);
	}
	return result;
}

} // namespace duckdb
