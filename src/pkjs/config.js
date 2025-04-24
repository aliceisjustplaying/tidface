module.exports = [
  {
    type: "heading",
    defaultValue: "Tidface Configuration",
  },
  {
    type: "text",
    defaultValue: "What should the clock be aligned to?",
  },

  {
    type: "section",
    items: [
      {
        type: "radiogroup",
        defaultValue: 0,
        label: "Time alignment mode",
        messageKey: "timeAlignmentMode",
        options: [
          {
            label: "Noon",
            value: 0,
          },
          {
            label: "It's 5 o'clock somewhere",
            value: 1,
          },
        ],
      },
    ],
  },
  {
    type: "submit",
    defaultValue: "Save",
  },
];
