export function valueScale(field) {
  return field.type === 8 ? (field.prec || 1) : 1;
}

export function displayedValue(field) {
  return field.value / valueScale(field);
}

export function wireValue(field, value) {
  return field.type === 8 ? Math.round(value * valueScale(field)) : value;
}
